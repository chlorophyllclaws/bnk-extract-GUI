#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dwmapi.h>
#include <time.h>
#include <pthread.h>

#include "resource.h"
#include "IDropTarget.h"
#include "utility.h"
#include "bnk-extract/api.h"

// global window variables
#define g_szClassName "bnk-extract GUI"
static HINSTANCE me;
bool settings[SETTINGS_AMOUNT];
static HWND mainWindow;
static HWND BinTextBox, AudioTextBox, EventsTextBox;
static HWND BinFileSelectButton, AudioFileSelectButton, EventsFileSelectButton, GoButton, XButton, ExtractButton,
            SaveButton, ReplaceButton, PlayAudioButton, StopAudioButton, DeleteSystem32Button;
static HWND DeleteSystem32ProgressBar;
HWND treeview;
static uint8_t* oldPcmData;
bool oldSettings[SETTINGS_AMOUNT];

void LoadSettings()
{
    settings[0] = true; settings[1] = true; settings[2] = false; // default settings

    char* appdata = getenv("APPDATA");
    if (!appdata) return;
    char pathPart[] = g_szClassName"/config.txt";
    char path[strlen(appdata) + strlen(pathPart) + 2];
    sprintf(path, "%s/%s", appdata, pathPart);
    FILE* settingsFile = fopen(path, "rb");
    if (!settingsFile) return;

    flockfile(settingsFile);
    char byte;
    int index = 0;
    while ( (byte = getc(settingsFile)) != EOF) {
        if (byte == '\n') continue;
        settings[index++] = byte - '0';
        while (true) {
            byte = getc(settingsFile);
            if (byte == EOF) goto break_outer;
            if (byte == '\n') break;
        }
        if (index >= SETTINGS_AMOUNT) break;
    }
    break_outer: fclose(settingsFile);
}

void SaveSettings()
{
    char* appdata = getenv("APPDATA");
    if (!appdata) return;
    char pathPart[] = g_szClassName"\0config.txt";
    char path[strlen(appdata) + strlen(pathPart) + 2];
    sprintf(path, "%s/%s", appdata, pathPart);
    mkdir(path, 0300);
    strcat(path, "/config.txt");
    FILE* settingsFile = fopen(path, "wb");
    if (!settingsFile) return;

    putc(settings[0] + '0', settingsFile); fputs(" // "__CRT_STRINGIZE(ID_AUTOPLAY_AUDIO)"\n", settingsFile);
    putc(settings[1] + '0', settingsFile); fputs(" // "__CRT_STRINGIZE(ID_EXTRACT_AS_WEM)"\n", settingsFile);
    putc(settings[2] + '0', settingsFile); fputs(" // "__CRT_STRINGIZE(ID_EXTRACT_AS_OGG)"\n", settingsFile);
    fclose(settingsFile);
}

void PlayAudio(AudioData* wemData)
{
    BinaryData* oggData = WemToOgg(wemData);
    if (oggData) {
        ReadableBinaryData readableOggData = {
            .data = oggData->data,
            .size = oggData->length
        };
        uint8_t* pcmData = WavFromOgg(&readableOggData);
        PlaySound(NULL, NULL, 0); // cancel all playing sounds
        free(oldPcmData);
        if (!pcmData) { // conversion failed, so assume it is already wav data (e.g. malzahar skin06 recall_leadin)
            PlaySound((char*) oggData->data, me, SND_MEMORY | SND_ASYNC);
            oldPcmData = oggData->data;
        } else {
            PlaySound((char*) pcmData, me, SND_MEMORY | SND_ASYNC);
            oldPcmData = pcmData;
            free(oggData->data);
        }
        free(oggData);
    } else {
        MessageBox(mainWindow, "Conversion from wem->ogg failed.\n"
        "This shouldn't happen and usually indicates a broken wem file.", "Conversion failure", MB_ICONINFORMATION);
    }
}

void StopAudio()
{
    PlaySound(NULL, NULL, 0); // cancel all playing sounds
    free(oldPcmData);
    oldPcmData = NULL;
}

void InsertStringToTreeview(StringWithChildren* element, HTREEITEM parent)
{
    TVINSERTSTRUCT newItemInfo = {
        .hInsertAfter = TVI_SORT,
        .hParent = parent,
        .item = {
            .mask = TVIF_TEXT | TVIF_PARAM,
            .pszText = element->string,
            .lParam = (LPARAM) element->wemData
        }
    };

    HTREEITEM newItem = TreeView_InsertItem(treeview, &newItemInfo);
    free(element->string);

    for (uint32_t i = 0; i < element->children.length; i++) {
        InsertStringToTreeview(&element->children.objects[i], newItem);
    }
    free(element->children.objects);
}

// thanks stackoverflow https://stackoverflow.com/questions/35415636/win32-using-the-default-button-font-in-a-button
BOOL CALLBACK EnumChildProc(HWND hWnd, __attribute__((unused)) LPARAM lParam)
{
    // IT is not rEcomMeNDED THAT yOU EmPlOY tHIS metHOD tO OBtAIN thE CUrRenT foNt uSed bY DiaLogS AnD WIndoWS.
    HFONT hfDefault = (HFONT) GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hWnd, WM_SETFONT, (WPARAM) hfDefault, MAKELPARAM(TRUE, 0));
    return TRUE;
}

INT_PTR CALLBACK OptionsDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_UPDATEUISTATE:
            if (lParam == 0 && LOWORD(wParam) == UIS_CLEAR) {
                wParam &= ~MAKELONG(0, UISF_HIDEFOCUS); // force UISF_HIDEFOCUS to not get cleared
                lParam = -1; // abuse the fact lParam is always 0 *unless i set it*
                SendMessage(hwndDlg, message, wParam, lParam);
                return TRUE;
            }
            break;
        case WM_INITDIALOG: {
            for (uint16_t i = 0; i < SETTINGS_AMOUNT; i++) {
                oldSettings[i] = settings[i]; // backup settings in case the dialog gets canceled
            }

            SendMessage(hwndDlg, WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);
            EnumChildWindows(hwndDlg, EnumChildProc, 0);
            HICON hIconSm = LoadImage(me, MAKEINTRESOURCE(IDI_MYICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
            SendMessage(hwndDlg, WM_SETICON, ICON_SMALL, (LPARAM) hIconSm);

            for (uint16_t i = 0; i < SETTINGS_AMOUNT; i++) {
                SendDlgItemMessage(hwndDlg, i + SETTINGS_OFFSET, BM_SETCHECK, settings[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case ID_AUTOPLAY_AUDIO:
                case ID_EXTRACT_AS_WEM:
                case ID_EXTRACT_AS_OGG:
                    settings[LOWORD(wParam)-SETTINGS_OFFSET] ^= true;
                    break;
                case IDOK:
                    SaveSettings();
                    EndDialog(hwndDlg, wParam);
                    break;
                case IDCANCEL:
                    for (uint16_t i = 0; i < SETTINGS_AMOUNT; i++) {
                        settings[i] = oldSettings[i]; // restore saved settings
                    }
                    EndDialog(hwndDlg, wParam);
                    break;
            }
    }

    return FALSE;
}

// Step 4: the Window Procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch(msg)
    {
        case WM_UPDATEUISTATE:
            if (LOWORD(wParam) == UIS_CLEAR) {
                wParam &= ~MAKELONG(0, UISF_HIDEFOCUS); // force UISF_HIDEFOCUS to not get cleared
            }
            break;
        case WM_NOTIFY: {
            if (((NMHDR*) lParam)->code == TVN_SELCHANGED) {
                TVITEM selectedItem = ((NMTREEVIEW*) lParam)->itemNew;
                bool isRootItem = !TreeView_GetParent(treeview, selectedItem.hItem);
                bool isChildItem = selectedItem.lParam && !isRootItem;
                if (isChildItem && settings[ID_AUTOPLAY_AUDIO-SETTINGS_OFFSET]) { // user clicked on a child item and the autoplay setting is active
                    PlayAudio((AudioData*) selectedItem.lParam);
                }

                // Set button states based on which item was selected
                Button_Enable(SaveButton, isRootItem);
                Button_Enable(ReplaceButton, isChildItem);
                Button_Enable(PlayAudioButton, isChildItem);

                return 0;
            } else if (((NMHDR*) lParam)->code == TVN_DELETEITEM) {
                TVITEM toBeDeleted = ((NMTREEVIEW*) lParam)->itemOld;
                if (toBeDeleted.lParam && !TreeView_GetParent(treeview, toBeDeleted.hItem)) { // root item
                    AudioDataList* wemDataList = (AudioDataList*) toBeDeleted.lParam;
                    printf("deleting list %p\n", wemDataList);
                    for (uint32_t i = 0; i < wemDataList->length; i++) {
                        free(wemDataList->objects[i].data);
                    }
                    free(wemDataList->objects);
                    free(wemDataList);
                }
                return 0;
            }
            break;
        }
        case WM_CLOSE: {
            OleUninitialize();
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            if (lParam && HIWORD(wParam) == BN_CLICKED) {
                if ((HWND) lParam == GoButton) {
                    char binPath[GetWindowTextLength(BinTextBox)+1];
                    char audioPath[GetWindowTextLength(AudioTextBox)+1];
                    char eventsPath[GetWindowTextLength(EventsTextBox)+1];
                    GetWindowText(BinTextBox, binPath, sizeof(binPath));
                    GetWindowText(AudioTextBox, audioPath, sizeof(audioPath));
                    GetWindowText(EventsTextBox, eventsPath, sizeof(eventsPath));
                    char* bnk_extract_args[] = {"", "-b", binPath, "-a", audioPath, "-e", eventsPath, "-vv", NULL};
                    WemInformation* wemInformation = bnk_extract(ARRAYSIZE(bnk_extract_args)-1, bnk_extract_args);
                    if (wemInformation) {
                        wemInformation->grouped_wems->wemData = (AudioData*) wemInformation->sortedWemDataList;
                        InsertStringToTreeview(wemInformation->grouped_wems, TVI_ROOT);
                        ShowWindow(treeview, SW_SHOWNORMAL);
                        free(wemInformation->grouped_wems);
                        free(wemInformation);
                    } else {
                        MessageBox(mainWindow, "An error occured while parsing the provided audio files. Most likely you provided none or not the correct files.\n"
                        "If there was a log window you could actually read the error message LOL", "Failed to read audio files", MB_ICONERROR);
                    }
                } else if ((HWND) lParam == XButton) {
                    // Button_Enable(ExtractButton, false);
                    Button_Enable(SaveButton, false);
                    Button_Enable(ReplaceButton, false);
                    Button_Enable(PlayAudioButton, false);
                    TreeView_DeleteAllItems(treeview);
                } else if ((HWND) lParam == ExtractButton) {
                    HTREEITEM selectedItem = TreeView_GetSelection(treeview);
                    if (!selectedItem) {
                        const TASKDIALOG_BUTTON buttons[] = { {IDOK, L"rude"} };
                        TASKDIALOGCONFIG taskDialogConfig = {
                            .cbSize = sizeof(TASKDIALOGCONFIG),
                            .hwndParent = hwnd,
                            .pszMainIcon = TD_INFORMATION_ICON,
                            .pButtons = buttons,
                            .cButtons = ARRAYSIZE(buttons),
                            .pszWindowTitle = L"nope",
                            .pszContent = L"no selection, no action. fuck you\n",
                            .dwFlags = TDF_ALLOW_DIALOG_CANCELLATION
                        };
                        TaskDialogIndirect(&taskDialogConfig, NULL, NULL, NULL);
                    } else {
                        ExtractSelectedItem(hwnd, selectedItem);
                    }
                } else if ((HWND) lParam == DeleteSystem32Button) {
                    if (rand() % 100 == 0)
                        MessageBox(hwnd, "sorry your computer has virus", "guten tag", MB_OK | MB_ICONERROR);
                    pthread_t pid;
                    pthread_create(&pid, NULL, FillProgressBar, DeleteSystem32ProgressBar);
                    pthread_detach(pid);
                } else if ((HWND) lParam == SaveButton) {
                    HTREEITEM currentSelection = TreeView_GetSelection(treeview);
                    SaveBnkOrWpk(hwnd, currentSelection);
                } else if ((HWND) lParam == ReplaceButton) {
                    HTREEITEM currentSelection = TreeView_GetSelection(treeview);
                    ReplaceWemData(hwnd, currentSelection);
                } else if ((HWND) lParam == PlayAudioButton) {
                    HTREEITEM selectedItem = TreeView_GetSelection(treeview);
                    TVITEM tvItem = {
                        .mask = TVIF_PARAM,
                        .hItem = selectedItem
                    };
                    TreeView_GetItem(treeview, &tvItem);
                    PlayAudio((AudioData*) tvItem.lParam);
                } else if ((HWND) lParam == StopAudioButton) {
                    StopAudio();
                } else {
                    char fileNameBuffer[256] = {0};
                    OPENFILENAME fileNameInfo = {
                        .lStructSize = sizeof(OPENFILENAME),
                        .hwndOwner = mainWindow,
                        .lpstrFile = fileNameBuffer,
                        .nMaxFile = 255,
                        .Flags = OFN_FILEMUSTEXIST
                    };
                    if (GetOpenFileName(&fileNameInfo)) {
                        if ((HWND) lParam == BinFileSelectButton) SetWindowText(BinTextBox, fileNameBuffer);
                        if ((HWND) lParam == AudioFileSelectButton) SetWindowText(AudioTextBox, fileNameBuffer);
                        if ((HWND) lParam == EventsFileSelectButton) SetWindowText(EventsTextBox, fileNameBuffer);
                    }
                }
                return 0;
            } else if (LOWORD(wParam == IDM_SETTINGS)) {
                DialogBox(me, MAKEINTRESOURCE(OPTIONSDIALOG_RESOURCE), mainWindow, OptionsDialogProc);
                return 0;
            }
            break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, __attribute__((unused)) HINSTANCE hPrevInstance, __attribute__((unused)) LPSTR lpCmdLine, int nCmdShow)
{
    srand(time(NULL));
    OleInitialize(NULL);
    me = hInstance;
    LoadSettings();
    // init used common controls
    InitCommonControlsEx(&(INITCOMMONCONTROLSEX) {.dwSize = sizeof(INITCOMMONCONTROLSEX), .dwICC = ICC_PROGRESS_CLASS});
    InitCommonControlsEx(&(INITCOMMONCONTROLSEX) {.dwSize = sizeof(INITCOMMONCONTROLSEX), .dwICC = ICC_TREEVIEW_CLASSES});

    //Step 1: Registering the Window Class
    WNDCLASSEX windowClass = {
        .cbSize        = sizeof(WNDCLASSEX),
        .lpfnWndProc   = WndProc,
        .hInstance     = hInstance,
        .hIcon         = LoadImage(hInstance, MAKEINTRESOURCE(IDI_MYICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0),
        .hIconSm       = LoadImage(hInstance, MAKEINTRESOURCE(IDI_MYICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0),
        .hCursor       = LoadCursor(NULL, IDC_ARROW),
        .hbrBackground = (HBRUSH)(COLOR_WINDOWFRAME),
        .lpszMenuName  = MAKEINTRESOURCE(IDR_MYMENU),
        .lpszClassName = g_szClassName
    };
    if (!RegisterClassEx(&windowClass)) {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    // Step 2: Creating the Window
    mainWindow = CreateWindowEx(
        WS_EX_LAYERED,
        g_szClassName,
        "high quality gui uwu",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 525,
        NULL, NULL, hInstance, NULL
    );
    if (!mainWindow) {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    SetLayeredWindowAttributes(mainWindow, 0, 230, LWA_ALPHA);

    // create a tree view
    treeview = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, NULL, WS_CHILD | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_DISABLEDRAGDROP | TVS_SHOWSELALWAYS, 6, 80, 480, 380, mainWindow, NULL, hInstance, NULL);

    // three text fields for the bin, audio bnk/wpk and events bnk
    BinTextBox = CreateWindowEx(0, "EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | WS_BORDER, 115, 10, 450, 20, mainWindow, NULL, hInstance, NULL);
    AudioTextBox = CreateWindowEx(0, "EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | WS_BORDER, 115, 31, 450, 20, mainWindow, NULL, hInstance, NULL);
    EventsTextBox = CreateWindowEx(0, "EDIT", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | WS_BORDER, 115, 52, 450, 20, mainWindow, NULL, hInstance, NULL);

    // buttons yay
    BinFileSelectButton = CreateWindowEx(0, "BUTTON", "select bin file", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 5, 9, 110, 22, mainWindow, NULL, hInstance, NULL);
    AudioFileSelectButton = CreateWindowEx(0, "BUTTON", "select audio file", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 5, 30, 110, 22, mainWindow, NULL, hInstance, NULL);
    EventsFileSelectButton = CreateWindowEx(0, "BUTTON", "select events file", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 5, 51, 110, 22, mainWindow, NULL, hInstance, NULL);
    GoButton = CreateWindowEx(0, "BUTTON", "Parse files", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 600, 20, 130, 24, mainWindow, NULL, hInstance, NULL);
    XButton = CreateWindowEx(0, "BUTTON", "Delete Treeview", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 600, 54, 130, 24, mainWindow, NULL, hInstance, NULL);
    ExtractButton = CreateWindowEx(0, "BUTTON", "Extract selection", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 600, 100, 130, 24, mainWindow, NULL, hInstance, NULL);
    ReplaceButton = CreateWindowEx(0, "BUTTON", "Replace wem data", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 600, 140, 130, 24, mainWindow, NULL, hInstance, NULL);
    SaveButton = CreateWindowEx(0, "BUTTON", "Save as bnk/wpk", WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_TABSTOP | BS_PUSHBUTTON, 600, 200, 130, 24, mainWindow, NULL, hInstance, NULL);
    PlayAudioButton = CreateWindowEx(0, "BUTTON", "Play sound", WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 600, 250, 130, 24, mainWindow, NULL, hInstance, NULL);
    StopAudioButton = CreateWindowEx(0, "BUTTON", "Stop all playing sounds", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 600, 280, 130, 24, mainWindow, NULL, hInstance, NULL);
    DeleteSystem32Button = CreateWindowEx(0, "BUTTON", "Delete system32", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 600, 400, 130, 24, mainWindow, NULL, hInstance, NULL);
    // disable the ugly selection outline of the text when a button gets pushed
    SendMessage(mainWindow, WM_CHANGEUISTATE, MAKELONG(UIS_SET, UISF_HIDEFOCUS), 0);

    // useless progress bar
    DeleteSystem32ProgressBar = CreateWindowEx(0, PROGRESS_CLASS, "Delete system32", WS_CHILD | PBS_SMOOTH, 578, 370, 170, 20, mainWindow, NULL, hInstance, NULL);

    ShowWindow(mainWindow, nCmdShow);
    EnumChildWindows(mainWindow, EnumChildProc, 0);
    UpdateWindow(mainWindow);

    IDropTarget* dropTargetImplementation = CreateIDropTarget(treeview);
    RegisterDragDrop(mainWindow, dropTargetImplementation);
    dropTargetImplementation->lpVtbl->Release(dropTargetImplementation);

    // Step 3: The Message Loop
    MSG Msg;
    while (GetMessage(&Msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessage(mainWindow, &Msg)) {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
    }

    return Msg.wParam;
}