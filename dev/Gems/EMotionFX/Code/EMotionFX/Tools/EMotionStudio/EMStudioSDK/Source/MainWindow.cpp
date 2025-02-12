/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include <EMotionStudio/EMStudioSDK/Source/DockWidgetPlugin.h>
#include <EMotionStudio/EMStudioSDK/Source/EMStudioManager.h>
#include <EMotionStudio/EMStudioSDK/Source/FileManager.h>
#include <EMotionStudio/EMStudioSDK/Source/KeyboardShortcutsWindow.h>
#include <EMotionStudio/EMStudioSDK/Source/LoadActorSettingsWindow.h>
#include <EMotionStudio/EMStudioSDK/Source/MainWindow.h>
#include <EMotionStudio/EMStudioSDK/Source/PluginManager.h>
#include <EMotionStudio/EMStudioSDK/Source/PreferencesWindow.h>
#include <EMotionStudio/EMStudioSDK/Source/RenderPlugin/RenderPlugin.h>
#include <EMotionStudio/EMStudioSDK/Source/ResetSettingsDialog.h>
#include <EMotionStudio/EMStudioSDK/Source/SaveChangedFilesManager.h>
#include <EMotionStudio/EMStudioSDK/Source/UnitScaleWindow.h>
#include <EMotionStudio/EMStudioSDK/Source/Workspace.h>

#include <Editor/ActorEditorBus.h>
#include <EMotionFX/CommandSystem/Source/MiscCommands.h>
#include <EMotionFX/CommandSystem/Source/SelectionCommands.h>
#include <AzFramework/StringFunc/StringFunc.h>

// include Qt related
#include <QAbstractEventDispatcher>
#include <QDesktopServices>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>

// include MCore related
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzFramework/API/ApplicationAPI.h>
#include <AzToolsFramework/API/EditorAssetSystemAPI.h>
#include <AzToolsFramework/AssetBrowser/AssetBrowserEntry.h>
#include <AzToolsFramework/UI/PropertyEditor/ReflectedPropertyEditor.hxx>
#include <EMotionFX/CommandSystem/Source/ActorCommands.h>
#include <EMotionFX/CommandSystem/Source/AnimGraphCommands.h>
#include <EMotionFX/CommandSystem/Source/MotionCommands.h>
#include <EMotionFX/CommandSystem/Source/MotionSetCommands.h>
#include <EMotionFX/CommandSystem/Source/SelectionList.h>
#include <EMotionFX/Source/ActorManager.h>
#include <EMotionFX/Source/AnimGraph.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/Importer/Importer.h>
#include <EMotionFX/Source/MotionManager.h>
#include <EMotionFX/Source/MotionSet.h>
AZ_PUSH_DISABLE_WARNING(4267, "-Wconversion")
#include <ISystem.h>
AZ_POP_DISABLE_WARNING
#include <LyViewPaneNames.h>
#include <MysticQt/Source/ComboBox.h>

// Include this on windows to detect device remove and insert messages, used for the game controller support.
#if defined(AZ_PLATFORM_WINDOWS)
    #include <dbt.h>
#endif


namespace EMStudio
{
    class NativeEventFilter
        : public QAbstractNativeEventFilter
    {
    public:
        NativeEventFilter(MainWindow* mainWindow)
            : QAbstractNativeEventFilter(),
            m_MainWindow(mainWindow)
        {
        }

        virtual bool nativeEventFilter(const QByteArray& /*eventType*/, void* message, long* /*result*/) Q_DECL_OVERRIDE;

    private:
        MainWindow * m_MainWindow;
    };

    class SaveDirtyWorkspaceCallback
        : public SaveDirtyFilesCallback
    {
        MCORE_MEMORYOBJECTCATEGORY(SaveDirtyWorkspaceCallback, MCore::MCORE_DEFAULT_ALIGNMENT, MEMCATEGORY_EMSTUDIOSDK)

    public:
        SaveDirtyWorkspaceCallback()
            : SaveDirtyFilesCallback()                                                              {}
        ~SaveDirtyWorkspaceCallback()                                                               {}

        enum
        {
            TYPE_ID = 0x000002345
        };
        uint32 GetType() const override                                                             { return TYPE_ID; }
        uint32 GetPriority() const override                                                         { return 0; }
        bool GetIsPostProcessed() const override                                                    { return false; }

        void GetDirtyFileNames(AZStd::vector<AZStd::string>* outFileNames, AZStd::vector<ObjectPointer>* outObjects) override
        {
            Workspace* workspace = GetManager()->GetWorkspace();
            if (workspace->GetDirtyFlag())
            {
                // add the filename to the dirty filenames array
                outFileNames->push_back(workspace->GetFilename());

                // add the link to the actual object
                ObjectPointer objPointer;
                objPointer.mWorkspace = workspace;
                outObjects->push_back(objPointer);
            }
        }

        int SaveDirtyFiles(const AZStd::vector<AZStd::string>& filenamesToSave, const AZStd::vector<ObjectPointer>& objects, MCore::CommandGroup* commandGroup) override
        {
            MCORE_UNUSED(filenamesToSave);

            const size_t numObjects = objects.size();
            for (size_t i = 0; i < numObjects; ++i)
            {
                // get the current object pointer and skip directly if the type check fails
                ObjectPointer objPointer = objects[i];
                if (objPointer.mWorkspace == nullptr)
                {
                    continue;
                }

                Workspace* workspace = objPointer.mWorkspace;

                // has the workspace been saved already or is it a new one?
                if (workspace->GetFilenameString().empty())
                {
                    // open up save as dialog so that we can choose a filename
                    const AZStd::string filename = GetMainWindow()->GetFileManager()->SaveWorkspaceFileDialog(GetMainWindow());
                    if (filename.empty())
                    {
                        return DirtyFileManager::CANCELED;
                    }

                    // save the workspace using the newly selected filename
                    AZStd::string command = AZStd::string::format("SaveWorkspace -filename \"%s\"", filename.c_str());
                    commandGroup->AddCommandString(command);
                }
                else
                {
                    // save workspace using its filename
                    AZStd::string command = AZStd::string::format("SaveWorkspace -filename \"%s\"", workspace->GetFilename());
                    commandGroup->AddCommandString(command);
                }
            }

            return DirtyFileManager::FINISHED;
        }

        const char* GetExtension() const override       { return "emfxworkspace"; }
        const char* GetFileType() const override        { return "workspace"; }
        const AZ::Uuid GetFileRttiType() const override
        {
            return azrtti_typeid<EMStudio::Workspace>();
        }

    };

    class UndoMenuCallback
        : public MCore::CommandManagerCallback
    {
    public:
        UndoMenuCallback(MainWindow* mainWindow)
            : m_mainWindow(mainWindow)
        {}
        ~UndoMenuCallback() = default;

        void OnRemoveCommand(uint32 historyIndex) override          { m_mainWindow->UpdateUndoRedo(); }
        void OnSetCurrentCommand(uint32 index) override             { m_mainWindow->UpdateUndoRedo(); }
        void OnAddCommandToHistory(uint32 historyIndex, MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine) override { m_mainWindow->UpdateUndoRedo(); }

        void OnPreExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine) override {}
        void OnPostExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine, bool wasSuccess, const AZStd::string& outResult) override {}
        void OnPreExecuteCommandGroup(MCore::CommandGroup* group, bool undo) override {}
        void OnPostExecuteCommandGroup(MCore::CommandGroup* group, bool wasSuccess) override {}
        void OnShowErrorReport(const AZStd::vector<AZStd::string>& errors) override {}

    private:
        MainWindow* m_mainWindow;
    };


    MainWindow::MainWindow(QWidget* parent, Qt::WindowFlags flags)
        : QMainWindow(parent, flags)
        , m_prevSelectedActor(nullptr)
        , m_prevSelectedActorInstance(nullptr)
        , m_undoMenuCallback(nullptr)
    {
        mLoadingOptions                 = false;
        mAutosaveTimer                  = nullptr;
        mPreferencesWindow              = nullptr;
        mApplicationMode                = nullptr;
        mDirtyFileManager               = nullptr;
        mFileManager                    = nullptr;
        mShortcutManager                = nullptr;
        mNativeEventFilter              = nullptr;
        mImportActorCallback            = nullptr;
        mRemoveActorCallback            = nullptr;
        mRemoveActorInstanceCallback    = nullptr;
        mImportMotionCallback           = nullptr;
        mRemoveMotionCallback           = nullptr;
        mCreateMotionSetCallback        = nullptr;
        mRemoveMotionSetCallback        = nullptr;
        mLoadMotionSetCallback          = nullptr;
        mCreateAnimGraphCallback        = nullptr;
        mRemoveAnimGraphCallback        = nullptr;
        mLoadAnimGraphCallback          = nullptr;
        mSelectCallback                 = nullptr;
        mUnselectCallback               = nullptr;
        m_clearSelectionCallback        = nullptr;
        mSaveWorkspaceCallback          = nullptr;
    }


    // destructor
    MainWindow::~MainWindow()
    {
        if (mNativeEventFilter)
        {
            QAbstractEventDispatcher::instance()->removeNativeEventFilter(mNativeEventFilter);
            delete mNativeEventFilter;
            mNativeEventFilter = nullptr;
        }

        if (mAutosaveTimer)
        {
            mAutosaveTimer->stop();
        }

        PluginOptionsNotificationsBus::Router::BusRouterDisconnect();
        SavePreferences();

        // Unload everything from the Editor, so that reopening the editor
        // results in an empty scene
        Reset();

        delete mShortcutManager;
        delete mFileManager;
        delete mDirtyFileManager;

        // unregister the command callbacks and get rid of the memory
        GetCommandManager()->RemoveCommandCallback(mImportActorCallback, false);
        GetCommandManager()->RemoveCommandCallback(mRemoveActorCallback, false);
        GetCommandManager()->RemoveCommandCallback(mRemoveActorInstanceCallback, false);
        GetCommandManager()->RemoveCommandCallback(mImportMotionCallback, false);
        GetCommandManager()->RemoveCommandCallback(mRemoveMotionCallback, false);
        GetCommandManager()->RemoveCommandCallback(mCreateMotionSetCallback, false);
        GetCommandManager()->RemoveCommandCallback(mRemoveMotionSetCallback, false);
        GetCommandManager()->RemoveCommandCallback(mLoadMotionSetCallback, false);
        GetCommandManager()->RemoveCommandCallback(mCreateAnimGraphCallback, false);
        GetCommandManager()->RemoveCommandCallback(mRemoveAnimGraphCallback, false);
        GetCommandManager()->RemoveCommandCallback(mLoadAnimGraphCallback, false);
        GetCommandManager()->RemoveCommandCallback(mSelectCallback, false);
        GetCommandManager()->RemoveCommandCallback(mUnselectCallback, false);
        GetCommandManager()->RemoveCommandCallback(m_clearSelectionCallback, false);
        GetCommandManager()->RemoveCommandCallback(mSaveWorkspaceCallback, false);
        GetCommandManager()->RemoveCallback(&m_mainWindowCommandManagerCallback, false);
        delete mImportActorCallback;
        delete mRemoveActorCallback;
        delete mRemoveActorInstanceCallback;
        delete mImportMotionCallback;
        delete mRemoveMotionCallback;
        delete mCreateMotionSetCallback;
        delete mRemoveMotionSetCallback;
        delete mLoadMotionSetCallback;
        delete mCreateAnimGraphCallback;
        delete mRemoveAnimGraphCallback;
        delete mLoadAnimGraphCallback;
        delete mSelectCallback;
        delete mUnselectCallback;
        delete m_clearSelectionCallback;
        delete mSaveWorkspaceCallback;

        EMotionFX::ActorEditorRequestBus::Handler::BusDisconnect();

        if (m_undoMenuCallback)
        {
            EMStudio::GetCommandManager()->RemoveCallback(m_undoMenuCallback);
        }
        EMotionFX::ActorEditorRequestBus::Handler::BusDisconnect();
    }

    void MainWindow::Reflect(AZ::ReflectContext* context)
    {
        GUIOptions::Reflect(context);
    }

    // init the main window
    void MainWindow::Init()
    {
        // tell the mystic Qt library about the main window
        MysticQt::GetMysticQt()->SetMainWindow(this);

        // enable drag&drop support
        setAcceptDrops(true);

        setDockNestingEnabled(true);

        setFocusPolicy(Qt::StrongFocus);

        CommandSystem::SelectionList& selectionList = GetCommandManager()->GetCurrentSelection();
        selectionList.Clear();

        // create the menu bar
        QWidget* menuWidget = new QWidget();
        QHBoxLayout* menuLayout = new QHBoxLayout(menuWidget);

        QMenuBar* menuBar = new QMenuBar(menuWidget);
        menuBar->setStyleSheet("QMenuBar { min-height: 10px;}"); // menu fix (to get it working with the Ly style)
        menuLayout->setMargin(0);
        menuLayout->setSpacing(0);
        menuLayout->addWidget(menuBar);

        QLabel* modeLabel = new QLabel("Layout: ");
        mApplicationMode = new MysticQt::ComboBox();
        menuLayout->addWidget(mApplicationMode);

        setMenuWidget(menuWidget);

        // file actions
        QMenu* menu = menuBar->addMenu(tr("&File"));

        // reset action
        mResetAction = menu->addAction(tr("&Reset"), this, &MainWindow::OnReset, QKeySequence::New);
        mResetAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/Refresh.png"));

        // save all
        mSaveAllAction = menu->addAction(tr("Save All..."), this, &MainWindow::OnSaveAll, QKeySequence::Save);
        mSaveAllAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/FileSave.png"));

        // disable the reset and save all menus until one thing is loaded
        mResetAction->setDisabled(true);
        mSaveAllAction->setDisabled(true);

        menu->addSeparator();

        // actor file actions
        QAction* openAction = menu->addAction(tr("&Open Actor"), this, &MainWindow::OnFileOpenActor, QKeySequence::Open);
        openAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Icons/Open.png"));
        mMergeActorAction = menu->addAction(tr("&Merge Actor"), this, &MainWindow::OnFileMergeActor, Qt::CTRL + Qt::Key_I);
        mMergeActorAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Icons/Open.png"));
        mSaveSelectedActorsAction = menu->addAction(tr("&Save Selected Actors"), this, &MainWindow::OnFileSaveSelectedActors);
        mSaveSelectedActorsAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/FileSave.png"));

        // disable the merge actor menu until one actor is in the scene
        DisableMergeActorMenu();

        // disable the save selected actor menu until one actor is selected
        DisableSaveSelectedActorsMenu();

        // recent actors submenu
        mRecentActors.Init(menu, mOptions.GetMaxRecentFiles(), "Recent Actors", "recentActorFiles");
        connect(&mRecentActors, &MysticQt::RecentFiles::OnRecentFile, this, &MainWindow::OnRecentFile);

        // workspace file actions
        menu->addSeparator();
        QAction* newWorkspaceAction = menu->addAction(tr("New Workspace"), this, &MainWindow::OnFileNewWorkspace);
        newWorkspaceAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Icons/Plus.png"));
        QAction* openWorkspaceAction = menu->addAction(tr("Open Workspace"), this, &MainWindow::OnFileOpenWorkspace);
        openWorkspaceAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Icons/Open.png"));
        QAction* saveWorkspaceAction = menu->addAction(tr("Save Workspace"), this, &MainWindow::OnFileSaveWorkspace);
        saveWorkspaceAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/FileSave.png"));
        QAction* saveWorkspaceAsAction = menu->addAction(tr("Save Workspace As"), this, &MainWindow::OnFileSaveWorkspaceAs);
        saveWorkspaceAsAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/FileSaveAs.png"));

        // recent workspace submenu
        mRecentWorkspaces.Init(menu, mOptions.GetMaxRecentFiles(), "Recent Workspaces", "recentWorkspaces");
        connect(&mRecentWorkspaces, &MysticQt::RecentFiles::OnRecentFile, this, &MainWindow::OnRecentFile);

        // edit menu
        menu = menuBar->addMenu(tr("&Edit"));
        m_undoAction = menu->addAction(
            MysticQt::GetMysticQt()->FindIcon("Images/Menu/Undo.png"),
            tr("Undo"),
            this,
            &MainWindow::OnUndo,
            QKeySequence::Undo
        );
        m_redoAction = menu->addAction(
            MysticQt::GetMysticQt()->FindIcon("Images/Menu/Redo.png"),
            tr("Redo"),
            this,
            &MainWindow::OnRedo,
            QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Z)
        );
        m_undoAction->setDisabled(true);
        m_redoAction->setDisabled(true);
        menu->addSeparator();
        QAction* preferencesAction = menu->addAction(tr("&Preferences"), this, &MainWindow::OnPreferences);
        preferencesAction->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Menu/Preferences.png"));

        // layouts item
        mLayoutsMenu = menuBar->addMenu(tr("&Layouts"));
        UpdateLayoutsMenu();

        // reset the application mode selection and connect it
        mApplicationMode->setCurrentIndex(-1);
        connect(mApplicationMode, static_cast<void (MysticQt::ComboBox::*)(const QString&)>(&MysticQt::ComboBox::currentIndexChanged), this, &MainWindow::ApplicationModeChanged);
        mLayoutLoaded = false;

        // view item
        menu = menuBar->addMenu(tr("&View"));
        mCreateWindowMenu = menu;

        // help menu
        menu = menuBar->addMenu(tr("&Help"));

        QMenu* folders = menu->addMenu("Folders");
        folders->setIcon(MysticQt::GetMysticQt()->FindIcon("Images/Icons/Open.png"));
        folders->addAction("Open autosave folder", this, &MainWindow::OnOpenAutosaveFolder);
        folders->addAction("Open settings folder", this, &MainWindow::OnOpenSettingsFolder);

        // Reset old workspace and start clean.
        GetManager()->GetWorkspace()->Reset();
        SetWindowTitleFromFileName("<not saved yet>");

        // create the autosave timer
        mAutosaveTimer = new QTimer(this);
        connect(mAutosaveTimer, &QTimer::timeout, this, &MainWindow::OnAutosaveTimeOut);

        // load preferences
        PluginOptionsNotificationsBus::Router::BusRouterConnect();
        LoadPreferences();

        // Create the dirty file manager and register the workspace callback.
        mDirtyFileManager = new DirtyFileManager;
        mDirtyFileManager->AddCallback(new SaveDirtyWorkspaceCallback);

        // init the file manager
        mFileManager = new EMStudio::FileManager(this);

        ////////////////////////////////////////////////////////////////////////
        // Keyboard Shortcut Manager
        ////////////////////////////////////////////////////////////////////////

        // create the shortcut manager
        mShortcutManager = new MysticQt::KeyboardShortcutManager();

        // load the old shortcuts
        QSettings shortcutSettings(AZStd::string(GetManager()->GetAppDataFolder() + "EMStudioKeyboardShortcuts.cfg").c_str(), QSettings::IniFormat, this);
        mShortcutManager->Load(&shortcutSettings);

        // add the application mode group
        const char* layoutGroupName = "Layouts";
        mShortcutManager->RegisterKeyboardShortcut("AnimGraph", layoutGroupName, Qt::Key_1, false, true, false);
        mShortcutManager->RegisterKeyboardShortcut("Animation", layoutGroupName, Qt::Key_2, false, true, false);
        mShortcutManager->RegisterKeyboardShortcut("Character", layoutGroupName, Qt::Key_3, false, true, false);

        EMotionFX::ActorEditorRequestBus::Handler::BusConnect();

        m_undoMenuCallback = new UndoMenuCallback(this);
        EMStudio::GetCommandManager()->RegisterCallback(m_undoMenuCallback);
        EMotionFX::ActorEditorRequestBus::Handler::BusConnect();

        // create and register the command callbacks
        mImportActorCallback = new CommandImportActorCallback(false);
        mRemoveActorCallback = new CommandRemoveActorCallback(false);
        mRemoveActorInstanceCallback = new CommandRemoveActorInstanceCallback(false);
        mImportMotionCallback = new CommandImportMotionCallback(false);
        mRemoveMotionCallback = new CommandRemoveMotionCallback(false);
        mCreateMotionSetCallback = new CommandCreateMotionSetCallback(false);
        mRemoveMotionSetCallback = new CommandRemoveMotionSetCallback(false);
        mLoadMotionSetCallback = new CommandLoadMotionSetCallback(false);
        mCreateAnimGraphCallback = new CommandCreateAnimGraphCallback(false);
        mRemoveAnimGraphCallback = new CommandRemoveAnimGraphCallback(false);
        mLoadAnimGraphCallback = new CommandLoadAnimGraphCallback(false);
        mSelectCallback = new CommandSelectCallback(false);
        mUnselectCallback = new CommandUnselectCallback(false);
        m_clearSelectionCallback = new CommandClearSelectionCallback(false);
        mSaveWorkspaceCallback = new CommandSaveWorkspaceCallback(false);
        GetCommandManager()->RegisterCommandCallback("ImportActor", mImportActorCallback);
        GetCommandManager()->RegisterCommandCallback("RemoveActor", mRemoveActorCallback);
        GetCommandManager()->RegisterCommandCallback("RemoveActorInstance", mRemoveActorInstanceCallback);
        GetCommandManager()->RegisterCommandCallback("ImportMotion", mImportMotionCallback);
        GetCommandManager()->RegisterCommandCallback("RemoveMotion", mRemoveMotionCallback);
        GetCommandManager()->RegisterCommandCallback("CreateMotionSet", mCreateMotionSetCallback);
        GetCommandManager()->RegisterCommandCallback("RemoveMotionSet", mRemoveMotionSetCallback);
        GetCommandManager()->RegisterCommandCallback("LoadMotionSet", mLoadMotionSetCallback);
        GetCommandManager()->RegisterCommandCallback("CreateAnimGraph", mCreateAnimGraphCallback);
        GetCommandManager()->RegisterCommandCallback("RemoveAnimGraph", mRemoveAnimGraphCallback);
        GetCommandManager()->RegisterCommandCallback("LoadAnimGraph", mLoadAnimGraphCallback);
        GetCommandManager()->RegisterCommandCallback("Select", mSelectCallback);
        GetCommandManager()->RegisterCommandCallback("Unselect", mUnselectCallback);
        GetCommandManager()->RegisterCommandCallback("ClearSelection", m_clearSelectionCallback);
        GetCommandManager()->RegisterCommandCallback("SaveWorkspace", mSaveWorkspaceCallback);

        GetCommandManager()->RegisterCallback(&m_mainWindowCommandManagerCallback);

        AZ_Assert(!mNativeEventFilter, "Double initialization?");
        mNativeEventFilter = new NativeEventFilter(this);
        QAbstractEventDispatcher::instance()->installNativeEventFilter(mNativeEventFilter);
    }

    void MainWindow::MainWindowCommandManagerCallback::OnPreExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        if (!AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandRecorderClear::s_RecorderClearCmdName, true) &&
            !AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandStopAllMotionInstances::s_stopAllMotionInstancesCmdName, true) &&
            !AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandSelect::s_SelectCmdName, true) &&
            !AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandUnselect::s_unselectCmdName, true) &&
            !AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandClearSelection::s_clearSelectionCmdName, true) &&
            !AzFramework::StringFunc::Equal(command->GetName(), CommandSystem::CommandToggleLockSelection::s_toggleLockSelectionCmdName, true) 
            )
        {
            AZStd::string commandResult;
            if (!GetCommandManager()->ExecuteCommandInsideCommand(CommandSystem::CommandRecorderClear::s_RecorderClearCmdName, commandResult))
            {
                AZ_Warning("Editor", false, "Clear recorder command failed: %s", commandResult.c_str());
            }
        }
    }

    void MainWindow::MainWindowCommandManagerCallback::OnPreUndoCommand(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        OnPreExecuteCommand(nullptr, command, commandLine);
    }

    bool NativeEventFilter::nativeEventFilter(const QByteArray& /*eventType*/, void* message, long* /*result*/)
    {
        #if defined(AZ_PLATFORM_WINDOWS)
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_DEVICECHANGE)
        {
            if (msg->wParam == DBT_DEVICEREMOVECOMPLETE || msg->wParam == DBT_DEVICEARRIVAL || msg->wParam == DBT_DEVNODES_CHANGED)
            {
                // The reason why there are multiple of such messages is because it emits messages for all related hardware nodes.
                // But we do not know the name of the hardware to look for here either, so we can't filter that.
                emit m_MainWindow->HardwareChangeDetected();
            }
        }
        #endif

        return false;
    }


    bool MainWindow::CommandImportActorCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();
        mainWindow->UpdateResetAndSaveAllMenus();

        return true;
    }


    bool MainWindow::CommandImportActorCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();
        mainWindow->UpdateResetAndSaveAllMenus();

        return true;
    }


    bool MainWindow::CommandRemoveActorCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();
        mainWindow->UpdateResetAndSaveAllMenus();

        return true;
    }


    bool MainWindow::CommandRemoveActorCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();
        mainWindow->UpdateResetAndSaveAllMenus();

        return true;
    }


    bool MainWindow::CommandRemoveActorInstanceCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandRemoveActorInstanceCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandImportMotionCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandImportMotionCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveMotionCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveMotionCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandCreateMotionSetCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandCreateMotionSetCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveMotionSetCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveMotionSetCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandLoadMotionSetCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandLoadMotionSetCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandCreateAnimGraphCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandCreateAnimGraphCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveAnimGraphCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandRemoveAnimGraphCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandLoadAnimGraphCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandLoadAnimGraphCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        GetManager()->GetMainWindow()->UpdateResetAndSaveAllMenus();
        return true;
    }


    bool MainWindow::CommandSelectCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandSelectCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandUnselectCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandUnselectCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandClearSelectionCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandClearSelectionCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);

        EMStudio::MainWindow* mainWindow = GetManager()->GetMainWindow();
        mainWindow->UpdateSaveActorsMenu();
        mainWindow->BroadcastSelectionNotifications();

        return true;
    }


    bool MainWindow::CommandSaveWorkspaceCallback::Execute(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        AZStd::string filename;
        commandLine.GetValue("filename", command, &filename);
        GetManager()->GetMainWindow()->OnWorkspaceSaved(filename.c_str());
        return true;
    }


    bool MainWindow::CommandSaveWorkspaceCallback::Undo(MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        return true;
    }


    void MainWindow::OnWorkspaceSaved(const char* filename)
    {
        mRecentWorkspaces.AddRecentFile(filename);
        SetWindowTitleFromFileName(filename);
    }


    void MainWindow::UpdateResetAndSaveAllMenus()
    {
        // enable the menus if at least one actor
        if (EMotionFX::GetActorManager().GetNumActors() > 0)
        {
            mResetAction->setEnabled(true);
            mSaveAllAction->setEnabled(true);
            return;
        }

        // enable the menus if at least one motion
        if (EMotionFX::GetMotionManager().GetNumMotions() > 0)
        {
            mResetAction->setEnabled(true);
            mSaveAllAction->setEnabled(true);
            return;
        }

        // enable the menus if at least one motion set
        if (EMotionFX::GetMotionManager().GetNumMotionSets() > 0)
        {
            mResetAction->setEnabled(true);
            mSaveAllAction->setEnabled(true);
            return;
        }

        // enable the menus if at least one anim graph
        if (EMotionFX::GetAnimGraphManager().GetNumAnimGraphs() > 0)
        {
            mResetAction->setEnabled(true);
            mSaveAllAction->setEnabled(true);
            return;
        }

        // nothing loaded, disable the menus
        mResetAction->setDisabled(true);
        mSaveAllAction->setDisabled(true);
    }


    void MainWindow::EnableMergeActorMenu()
    {
        mMergeActorAction->setEnabled(true);
    }


    void MainWindow::DisableMergeActorMenu()
    {
        mMergeActorAction->setDisabled(true);
    }


    void MainWindow::UpdateSaveActorsMenu()
    {
        // enable the merge menu only if one actor is in the scene
        if (EMotionFX::GetActorManager().GetNumActors() > 0)
        {
            EnableMergeActorMenu();
        }
        else
        {
            DisableMergeActorMenu();
        }

        // enable the actor save selected menu only if one actor or actor instance is selected
        // it's needed to check here because if one actor is removed it's not selected anymore
        const CommandSystem::SelectionList& selectionList = GetCommandManager()->GetCurrentSelection();
        const uint32 numSelectedActors = selectionList.GetNumSelectedActors();
        const uint32 numSelectedActorInstances = selectionList.GetNumSelectedActorInstances();
        if ((numSelectedActors > 0) || (numSelectedActorInstances > 0))
        {
            EnableSaveSelectedActorsMenu();
        }
        else
        {
            DisableSaveSelectedActorsMenu();
        }
    }


    void MainWindow::EnableSaveSelectedActorsMenu()
    {
        mSaveSelectedActorsAction->setEnabled(true);
    }


    void MainWindow::DisableSaveSelectedActorsMenu()
    {
        mSaveSelectedActorsAction->setDisabled(true);
    }


    void MainWindow::SetWindowTitleFromFileName(const AZStd::string& fileName)
    {
        // get only the version number of EMotion FX
        AZStd::string emfxVersionString = EMotionFX::GetEMotionFX().GetVersionString();
        AzFramework::StringFunc::Replace(emfxVersionString, "EMotion FX ", "", true /* case sensitive */);
        
        // set the window title
        // only set the EMotion FX version if the filename is empty
        AZStd::string windowTitle;
        windowTitle = AZStd::string::format("EMotion Studio %s (BUILD %s)", emfxVersionString.c_str(), MCORE_DATE);
        if (fileName.empty() == false)
        {
            windowTitle += AZStd::string::format(" - %s", fileName.c_str());
        }
        setWindowTitle(windowTitle.c_str());
    }


    // update the items inside the Window->Create menu item
    void MainWindow::UpdateCreateWindowMenu()
    {
        // get the plugin manager
        PluginManager* pluginManager = GetPluginManager();

        // get the number of plugins
        const uint32 numPlugins = pluginManager->GetNumPlugins();

        // add each plugin name in an array to sort them
        MCore::Array<AZStd::string> sortedPlugins;
        sortedPlugins.Reserve(numPlugins);
        for (uint32 p = 0; p < numPlugins; ++p)
        {
            EMStudioPlugin* plugin = pluginManager->GetPlugin(p);
            sortedPlugins.Add(plugin->GetName());
        }
        sortedPlugins.Sort();

        // clear the window menu
        mCreateWindowMenu->clear();

        // for all registered plugins, create a menu items
        for (uint32 p = 0; p < numPlugins; ++p)
        {
            // get the plugin
            const uint32 pluginIndex = pluginManager->FindPluginByTypeString(sortedPlugins[p].c_str());
            EMStudioPlugin* plugin = pluginManager->GetPlugin(pluginIndex);

            // don't add invisible plugins to the list
            if (plugin->GetPluginType() == EMStudioPlugin::PLUGINTYPE_INVISIBLE)
            {
                continue;
            }

            // check if multiple instances allowed
            // on this case the plugin is not one action but one submenu
            if (plugin->AllowMultipleInstances())
            {
                // create the menu
                mCreateWindowMenu->addMenu(plugin->GetName());

                // TODO: add each instance inside the submenu
            }
            else
            {
                // create the action
                QAction* action = mCreateWindowMenu->addAction(plugin->GetName());
                action->setData(plugin->GetName());

                // connect the action to activate the plugin when clicked on it
                connect(action, &QAction::triggered, this, &MainWindow::OnWindowCreate);

                // set the action checkable
                action->setCheckable(true);

                // set the checked state of the action
                EMStudioPlugin* activePlugin = pluginManager->FindActivePlugin(plugin->GetClassID());
                action->setChecked(activePlugin != nullptr);

                // Create any children windows this plugin might want to create
                if (activePlugin)
                {
                    // must use the active plugin, as it needs to be initialized to create window entries
                    activePlugin->AddWindowMenuEntries(mCreateWindowMenu);
                }
            }
        }
    }


    // create a new given window
    void MainWindow::OnWindowCreate(bool checked)
    {
        // get the plugin name
        QAction* action = (QAction*)sender();
        const QString pluginName = action->data().toString();

        // checked is the new state
        // activate the plugin if the menu is not checked
        // show and focus on the actual window if the menu is already checked
        if (checked)
        {
            // try to create the new window
            EMStudioPlugin* newPlugin = EMStudio::GetPluginManager()->CreateWindowOfType(FromQtString(pluginName).c_str());
            if (newPlugin == nullptr)
            {
                MCore::LogError("Failed to create window using plugin '%s'", FromQtString(pluginName).c_str());
                return;
            }

            // if we have a dock widget plugin here, making floatable and change its window size
            if (newPlugin->GetPluginType() == EMStudioPlugin::PLUGINTYPE_DOCKWIDGET)
            {
                DockWidgetPlugin* dockPlugin = static_cast<DockWidgetPlugin*>(newPlugin);
                dockPlugin->GetDockWidget()->setFloating(true);
                const QSize s = dockPlugin->GetInitialWindowSize();
                dockPlugin->GetDockWidget()->resize(s.width(), s.height());
            }
        }
        else // (checked == false)
        {
            EMStudioPlugin* plugin = EMStudio::GetPluginManager()->GetActivePluginByTypeString(FromQtString(pluginName).c_str());
            AZ_Assert(plugin, "Failed to get plugin, since it was checked it should be active");
            EMStudio::GetPluginManager()->RemoveActivePlugin(plugin);
        }

        // update the window menu
        UpdateCreateWindowMenu();
    }

    // open the autosave folder
    void MainWindow::OnOpenAutosaveFolder()
    {
        const QUrl url(("file:///" + GetManager()->GetAutosavesFolder()).c_str());
        QDesktopServices::openUrl(url);
    }


    // open the settings folder
    void MainWindow::OnOpenSettingsFolder()
    {
        const QUrl url(("file:///" + GetManager()->GetAppDataFolder()).c_str());
        QDesktopServices::openUrl(url);
    }

    // show the preferences dialog
    void MainWindow::OnPreferences()
    {
        if (mPreferencesWindow == nullptr)
        {
            mPreferencesWindow = new PreferencesWindow(this);
            mPreferencesWindow->Init();

            AzToolsFramework::ReflectedPropertyEditor* generalPropertyWidget = mPreferencesWindow->AddCategory("General", "Images/Preferences/General.png", false);
            generalPropertyWidget->ClearInstances();
            generalPropertyWidget->InvalidateAll();

            generalPropertyWidget->AddInstance(&mOptions, azrtti_typeid(mOptions));

            PluginManager* pluginManager = GetPluginManager();
            const uint32 numPlugins = pluginManager->GetNumActivePlugins();
            for (uint32 i = 0; i < numPlugins; ++i)
            {
                EMStudioPlugin* currentPlugin = pluginManager->GetActivePlugin(i);
                PluginOptions* pluginOptions = currentPlugin->GetOptions();
                if (pluginOptions)
                {
                    generalPropertyWidget->AddInstance(pluginOptions, azrtti_typeid(pluginOptions));
                }
            }

            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
            if (!serializeContext)
            {
                AZ_Error("EMotionFX", false, "Can't get serialize context from component application.");
                return;
            }
            generalPropertyWidget->SetAutoResizeLabels(true);
            generalPropertyWidget->Setup(serializeContext, nullptr, true);
            generalPropertyWidget->show();
            generalPropertyWidget->ExpandAll();
            generalPropertyWidget->InvalidateAll();

            // Keyboard shortcuts
            KeyboardShortcutsWindow* shortcutsWindow = new KeyboardShortcutsWindow(mPreferencesWindow);
            mPreferencesWindow->AddCategory(shortcutsWindow, "Keyboard\nShortcuts", "Images/Preferences/KeyboardShortcuts.png", false);
        }

        mPreferencesWindow->exec();
        SavePreferences();
    }


    // save the preferences
    void MainWindow::SavePreferences()
    {
        // open the config file
        QSettings settings(this);
        mOptions.Save(settings, *this);
    }


    // load the preferences
    void MainWindow::LoadPreferences()
    {
        // When a setting changes, OnOptionChanged will save. To avoid saving while settings are being
        // loaded, we use this flag
        mLoadingOptions = true;

        // open the config file
        QSettings settings(this);
        mOptions = GUIOptions::Load(settings, *this);

        mLoadingOptions = false;
    }


    void MainWindow::LoadActor(const char* fileName, bool replaceCurrentScene)
    {
        // create the final command
        AZStd::string commandResult;

        // set the command group name based on the parameters
        const AZStd::string commandGroupName = (replaceCurrentScene) ? "Open actor" : "Merge actor";

        // create the command group
        AZStd::string outResult;
        MCore::CommandGroup commandGroup(commandGroupName.c_str());

        // clear the scene if not merging
        // clear the actors and actor instances selection if merging
        if (replaceCurrentScene)
        {
            CommandSystem::ClearScene(true, true, &commandGroup);
        }
        else
        {
            commandGroup.AddCommandString("Unselect -actorInstanceID SELECT_ALL -actorID SELECT_ALL");
        }

        // create the load command
        AZStd::string loadActorCommand;

        // add the import command
        loadActorCommand = AZStd::string::format("ImportActor -filename \"%s\" ", fileName);

        // add the load actor settings
        LoadActorSettingsWindow::LoadActorSettings loadActorSettings;
        loadActorCommand += "-loadMeshes " + AZStd::to_string(loadActorSettings.mLoadMeshes);
        loadActorCommand += " -loadTangents " + AZStd::to_string(loadActorSettings.mLoadTangents);
        loadActorCommand += " -autoGenTangents " + AZStd::to_string(loadActorSettings.mAutoGenerateTangents);
        loadActorCommand += " -loadLimits " + AZStd::to_string(loadActorSettings.mLoadLimits);
        loadActorCommand += " -loadGeomLods " + AZStd::to_string(loadActorSettings.mLoadGeometryLODs);
        loadActorCommand += " -loadMorphTargets " + AZStd::to_string(loadActorSettings.mLoadMorphTargets);
        loadActorCommand += " -loadCollisionMeshes " + AZStd::to_string(loadActorSettings.mLoadCollisionMeshes);
        loadActorCommand += " -loadMaterialLayers " + AZStd::to_string(loadActorSettings.mLoadStandardMaterialLayers);
        loadActorCommand += " -loadSkinningInfo " + AZStd::to_string(loadActorSettings.mLoadSkinningInfo);
        loadActorCommand += " -loadSkeletalLODs " + AZStd::to_string(loadActorSettings.mLoadSkeletalLODs);
        loadActorCommand += " -dualQuatSkinning " + AZStd::to_string(loadActorSettings.mDualQuaternionSkinning);

        // add the load and the create instance commands
        commandGroup.AddCommandString(loadActorCommand.c_str());
        commandGroup.AddCommandString("CreateActorInstance -actorID %LASTRESULT%");

        // if the current scene is replaced or merge on an empty scene, focus on the new actor instance
        if (replaceCurrentScene || EMotionFX::GetActorManager().GetNumActorInstances() == 0)
        {
            commandGroup.AddCommandString("ReInitRenderActors -resetViewCloseup true");
        }

        // execute the group command
        if (GetCommandManager()->ExecuteCommandGroup(commandGroup, outResult) == false)
        {
            MCore::LogError("Could not load actor '%s'.", fileName);
        }

        // add the actor in the recent actor list
        // if the same actor is already in the list, the duplicate is removed
        mRecentActors.AddRecentFile(fileName);
    }



    void MainWindow::LoadCharacter(const AZ::Data::AssetId& actorAssetId, const AZ::Data::AssetId& animgraphId, const AZ::Data::AssetId& motionSetId)
    {
        mCharacterFiles.clear();
        AZStd::string cachePath = gEnv->pFileIO->GetAlias("@assets@");
        AZStd::string filename;
        AzFramework::StringFunc::AssetDatabasePath::Normalize(cachePath);

        AZStd::string actorFilename;
        EBUS_EVENT_RESULT(actorFilename, AZ::Data::AssetCatalogRequestBus, GetAssetPathById, actorAssetId);
        AzFramework::StringFunc::AssetDatabasePath::Join(cachePath.c_str(), actorFilename.c_str(), filename, true);
        actorFilename = filename;

        AZStd::string animgraphFilename;
        EBUS_EVENT_RESULT(animgraphFilename, AZ::Data::AssetCatalogRequestBus, GetAssetPathById, animgraphId);
        bool found;
        if (!animgraphFilename.empty())
        {
            EBUS_EVENT_RESULT(found, AzToolsFramework::AssetSystemRequestBus, GetFullSourcePathFromRelativeProductPath, animgraphFilename.c_str(), filename);
            if (found)
            {
                animgraphFilename = filename;
            }
        }

        AZStd::string motionSetFilename;
        EBUS_EVENT_RESULT(motionSetFilename, AZ::Data::AssetCatalogRequestBus, GetAssetPathById, motionSetId);
        if (!motionSetFilename.empty())
        {
            EBUS_EVENT_RESULT(found, AzToolsFramework::AssetSystemRequestBus, GetFullSourcePathFromRelativeProductPath, motionSetFilename.c_str(), filename);
            if (found)
            {
                motionSetFilename = filename;
            }
        }

        // if the name is empty we stop looking for it
        bool foundActor = actorFilename.empty();
        bool foundAnimgraph = animgraphFilename.empty();
        bool foundMotionSet = motionSetFilename.empty();

        // Gather the list of dirty files
        AZStd::vector<AZStd::string> filenames;
        AZStd::vector<SaveDirtyFilesCallback::ObjectPointer> objects;
        AZStd::vector<SaveDirtyFilesCallback::ObjectPointer> dirtyObjects;

        const size_t numDirtyFilesCallbacks = mDirtyFileManager->GetNumCallbacks();
        for (size_t i = 0; i < numDirtyFilesCallbacks; ++i)
        {
            SaveDirtyFilesCallback* callback = mDirtyFileManager->GetCallback(i);
            callback->GetDirtyFileNames(&filenames, &objects);
            const size_t numFileNames = filenames.size();
            for (size_t j = 0; j < numFileNames; ++j)
            {
                // bypass if the filename is empty
                // it's the case when the file is not already saved
                if (filenames[j].empty())
                {
                    continue;
                }

                if (!foundActor && filenames[j] == actorFilename)
                {
                    foundActor = true;
                }
                else if (!foundAnimgraph && filenames[j] == animgraphFilename)
                {
                    foundAnimgraph = true;
                }
                else if (!foundMotionSet && filenames[j] == motionSetFilename)
                {
                    foundMotionSet = true;
                }
            }
        }

        // Dont reload dirty files that are already open.
        if (!foundActor)
        {
            mCharacterFiles.push_back(actorFilename);
        }
        if (!foundAnimgraph)
        {
            mCharacterFiles.push_back(animgraphFilename);
        }
        if (!foundMotionSet)
        {
            mCharacterFiles.push_back(motionSetFilename);
        }

        if (isVisible() && mLayoutLoaded)
        {
            LoadCharacterFiles();
        }
    }

    void MainWindow::OnFileNewWorkspace()
    {
        // save all files that have been changed
        if (mDirtyFileManager->SaveDirtyFiles() == DirtyFileManager::CANCELED)
        {
            return;
        }

        // Are you sure?
        if (QMessageBox::warning(this, "New Workspace", "Are you sure you want to create a new workspace?\n\nThis will reset the entire scene.\n\nClick Yes to reset the scene and create a new workspace, No in case you want to cancel the process.", QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
        {
            return;
        }

        // create th command group
        MCore::CommandGroup commandGroup("New workspace", 32);

        // clear everything
        Reset(true, true, true, true, &commandGroup);

        // execute the group command
        AZStd::string result;
        if (GetCommandManager()->ExecuteCommandGroup(commandGroup, result))
        {
            // clear the history
            GetCommandManager()->ClearHistory();

            // set the window title to not saved yet
            SetWindowTitleFromFileName("<not saved yet>");
        }
        else
        {
            AZ_Error("EMotionFX", false, result.c_str());
        }

        Workspace* workspace = GetManager()->GetWorkspace();
        workspace->SetFilename("");
        workspace->SetDirtyFlag(false);
    }


    void MainWindow::OnFileOpenWorkspace()
    {
        const AZStd::string filename = mFileManager->LoadWorkspaceFileDialog(this);
        if (filename.empty())
        {
            return;
        }

        LoadFile(filename.c_str());
    }


    void MainWindow::OnSaveAll()
    {
        mDirtyFileManager->SaveDirtyFiles(MCORE_INVALIDINDEX32, MCORE_INVALIDINDEX32, QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    }


    void MainWindow::OnFileSaveWorkspace()
    {
        // save all files that have been changed, filter to not show the workspace files
        if (mDirtyFileManager->SaveDirtyFiles(MCORE_INVALIDINDEX32, SaveDirtyWorkspaceCallback::TYPE_ID) == DirtyFileManager::CANCELED)
        {
            return;
        }

        Workspace* workspace = GetManager()->GetWorkspace();
        AZStd::string command;

        // save using the current filename or show the dialog
        if (workspace->GetFilenameString().empty())
        {
            // open up save as dialog so that we can choose a filename
            const AZStd::string filename = GetMainWindow()->GetFileManager()->SaveWorkspaceFileDialog(GetMainWindow());
            if (filename.empty())
            {
                return;
            }

            // save the workspace using the newly selected filename
            command = AZStd::string::format("SaveWorkspace -filename \"%s\"", filename.c_str());
        }
        else
        {
            command = AZStd::string::format("SaveWorkspace -filename \"%s\"", workspace->GetFilename());
        }
        AZStd::string result;
        if (EMStudio::GetCommandManager()->ExecuteCommand(command, result))
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_SUCCESS, 
                "Workspace <font color=green>successfully</font> saved");
        }
        else
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_ERROR, 
                AZStd::string::format("Workspace <font color=red>failed</font> to save<br/><br/>%s", result.c_str()).c_str());
        }
    }


    void MainWindow::OnFileSaveWorkspaceAs()
    {
        // save all files that have been changed, filter to not show the workspace files
        if (mDirtyFileManager->SaveDirtyFiles(MCORE_INVALIDINDEX32, SaveDirtyWorkspaceCallback::TYPE_ID) == DirtyFileManager::CANCELED)
        {
            return;
        }

        // open up save as dialog so that we can choose a filename
        const AZStd::string filename = GetMainWindow()->GetFileManager()->SaveWorkspaceFileDialog(GetMainWindow());
        if (filename.empty())
        {
            return;
        }

        // save the workspace using the newly selected filename
        const AZStd::string command = AZStd::string::format("SaveWorkspace -filename \"%s\"", filename.c_str());

        AZStd::string result;
        if (EMStudio::GetCommandManager()->ExecuteCommand(command, result))
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_SUCCESS, 
                "Workspace <font color=green>successfully</font> saved");
        }
        else
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_ERROR, 
                AZStd::string::format("Workspace <font color=red>failed</font> to save<br/><br/>%s", result.c_str()).c_str());
        }
    }


    void MainWindow::Reset(bool clearActors, bool clearMotionSets, bool clearMotions, bool clearAnimGraphs, MCore::CommandGroup* commandGroup)
    {
        // create and relink to a temporary new command group in case the input command group has not been specified
        MCore::CommandGroup newCommandGroup("Reset Scene");

        // add commands in the command group if one is valid
        if (commandGroup == nullptr)
        {
            if (clearActors)
            {
                CommandSystem::ClearScene(true, true, &newCommandGroup);
            }
            if (clearAnimGraphs)
            {
                CommandSystem::ClearAnimGraphsCommand(&newCommandGroup);
            }
            if (clearMotionSets)
            {
                CommandSystem::ClearMotionSetsCommand(&newCommandGroup);
            }
            if (clearMotions)
            {
                CommandSystem::ClearMotions(&newCommandGroup, true);
            }
        }
        else
        {
            if (clearActors)
            {
                CommandSystem::ClearScene(true, true, commandGroup);
            }
            if (clearAnimGraphs)
            {
                CommandSystem::ClearAnimGraphsCommand(commandGroup);
            }
            if (clearMotionSets)
            {
                CommandSystem::ClearMotionSetsCommand(commandGroup);
            }
            if (clearMotions)
            {
                CommandSystem::ClearMotions(commandGroup, true);
            }
        }

        if (!commandGroup)
        {
            AZStd::string result;
            if (!GetCommandManager()->ExecuteCommandGroup(newCommandGroup, result))
            {
                AZ_Error("EMotionFX", false, result.c_str());
            }
        }

        GetCommandManager()->ClearHistory();

        Workspace* workspace = GetManager()->GetWorkspace();
        workspace->SetDirtyFlag(true);
    }   

    void MainWindow::OnReset()
    {
        if (mDirtyFileManager->SaveDirtyFiles() == DirtyFileManager::CANCELED)
        {
            return;
        }

        ResetSettingsDialog resetDialog(this);
        if (resetDialog.exec() == QDialog::Accepted)
        {
            Reset(
                resetDialog.IsActorsChecked(),
                resetDialog.IsMotionSetsChecked(),
                resetDialog.IsMotionsChecked(),
                resetDialog.IsAnimGraphsChecked()
            );
        }
    }

    void MainWindow::OnOptionChanged(const AZStd::string& optionChanged)
    {
        if (optionChanged == GUIOptions::s_maxRecentFilesOptionName)
        {
            // Set the maximum number of recent files
            mRecentActors.SetMaxRecentFiles(mOptions.GetMaxRecentFiles());
            mRecentWorkspaces.SetMaxRecentFiles(mOptions.GetMaxRecentFiles());
        }
        else if (optionChanged == GUIOptions::s_maxHistoryItemsOptionName)
        {
            // Set the maximum number of history items in the command manager
            GetCommandManager()->SetMaxHistoryItems(mOptions.GetMaxHistoryItems());
        }
        else if (optionChanged == GUIOptions::s_notificationVisibleTimeOptionName)
        {
            // Set the notification visible time
            GetNotificationWindowManager()->SetVisibleTime(mOptions.GetNotificationInvisibleTime());
        }
        else if (optionChanged == GUIOptions::s_enableAutosaveOptionName)
        {
            // Enable or disable the autosave timer
            if (mOptions.GetEnableAutoSave())
            {
                mAutosaveTimer->start();
            }
            else
            {
                mAutosaveTimer->stop();
            }
        }
        else if (optionChanged == GUIOptions::s_autosaveIntervalOptionName)
        {
            // Set the autosave interval
            mAutosaveTimer->stop();
            mAutosaveTimer->setInterval(mOptions.GetAutoSaveInterval() * 60 * 1000);
            mAutosaveTimer->start();
        }
        else if (optionChanged == GUIOptions::s_importerLogDetailsEnabledOptionName)
        {
            // Set if the detail logging of the importer is enabled or not
            EMotionFX::GetImporter().SetLogDetails(mOptions.GetImporterLogDetailsEnabled());
        }
        else if (optionChanged == GUIOptions::s_autoLoadLastWorkspaceOptionName)
        {
            // Set if auto loading the last workspace is enabled or not
            GetManager()->SetAutoLoadLastWorkspace(mOptions.GetAutoLoadLastWorkspace());
        }

        // Save preferences
        if (!mLoadingOptions)
        {
            SavePreferences();
        }
    }

    // open an actor
    void MainWindow::OnFileOpenActor()
    {

        if (mDirtyFileManager->SaveDirtyFiles({azrtti_typeid<EMotionFX::Actor>()}) == DirtyFileManager::CANCELED)
        {
            return;
        }

        AZStd::vector<AZStd::string> filenames = mFileManager->LoadActorsFileDialog(this);
        if (filenames.empty())
        {
            return;
        }

        const size_t numFilenames = filenames.size();
        for (size_t i = 0; i < numFilenames; ++i)
        {
            LoadActor(filenames[i].c_str(), i == 0);
        }
    }


    // merge an actor
    void MainWindow::OnFileMergeActor()
    {
        AZStd::vector<AZStd::string> filenames = mFileManager->LoadActorsFileDialog(this);
        if (filenames.empty())
        {
            return;
        }

        for (const AZStd::string& filename : filenames)
        {
            LoadActor(filename.c_str(), false);
        }
    }


    // save selected actors
    void MainWindow::OnFileSaveSelectedActors()
    {
        // get the current selection list
        const CommandSystem::SelectionList& selectionList             = GetCommandManager()->GetCurrentSelection();
        const uint32                        numSelectedActors         = selectionList.GetNumSelectedActors();
        const uint32                        numSelectedActorInstances = selectionList.GetNumSelectedActorInstances();

        // create the saving actor array
        AZStd::vector<EMotionFX::Actor*> savingActors;
        savingActors.reserve(numSelectedActors + numSelectedActorInstances);

        // add all selected actors to the list
        for (uint32 i = 0; i < numSelectedActors; ++i)
        {
            savingActors.push_back(selectionList.GetActor(i));
        }

        // check all actors of all selected actor instances and put them in the list if they are not in yet
        for (uint32 i = 0; i < numSelectedActorInstances; ++i)
        {
            EMotionFX::Actor* actor = selectionList.GetActorInstance(i)->GetActor();

            if (actor->GetIsOwnedByRuntime())
            {
                continue;
            }

            if (AZStd::find(savingActors.begin(), savingActors.end(), actor) == savingActors.end())
            {
                savingActors.push_back(actor);
            }
        }

        // Save all selected actors.
        const size_t numActors = savingActors.size();
        for (size_t i = 0; i < numActors; ++i)
        {
            EMotionFX::Actor* actor = savingActors[i];
            GetMainWindow()->GetFileManager()->SaveActor(actor);
        }
    }


    void MainWindow::OnRecentFile(QAction* action)
    {
        const AZStd::string filename = action->data().toString().toUtf8().data();

        // Load the recent file.
        // No further error handling needed here as the commands do that all internally.
        LoadFile(filename.c_str(), 0, 0, false);
    }


    // save the current layout to a file
    void MainWindow::OnLayoutSaveAs()
    {
        EMStudio::GetLayoutManager()->SaveLayoutAs();
    }


    // update the layouts menu
    void MainWindow::UpdateLayoutsMenu()
    {
        // clear the current menu
        mLayoutsMenu->clear();

        // generate the layouts path
        QString layoutsPath = MysticQt::GetDataDir().c_str();
        layoutsPath += "Layouts/";

        // open the dir
        QDir dir(layoutsPath);
        dir.setFilter(QDir::Files | QDir::NoSymLinks);
        dir.setSorting(QDir::Name);

        // add each layout
        mLayoutNames.Clear();
        AZStd::string filename;
        const QFileInfoList list = dir.entryInfoList();
        const int listSize = list.size();
        for (int i = 0; i < listSize; ++i)
        {
            // get the filename
            const QFileInfo fileInfo = list[i];
            FromQtString(fileInfo.fileName(), &filename);

            // check the extension, only ".layout" are accepted
            AZStd::string extension;
            AzFramework::StringFunc::Path::GetExtension(filename.c_str(), extension, false /* include dot */);
            AZStd::to_lower(extension.begin(), extension.end());
            if (extension == "layout")
            {
                AzFramework::StringFunc::Path::GetFileName(filename.c_str(), filename);
                mLayoutNames.Add(filename);
            }
        }

        // add each menu
        const uint32 numLayoutNames = mLayoutNames.GetLength();
        for (uint32 i = 0; i < numLayoutNames; ++i)
        {
            QAction* action = mLayoutsMenu->addAction(mLayoutNames[i].c_str());
            connect(action, &QAction::triggered, this, &MainWindow::OnLoadLayout);
        }

        // add the separator only if at least one layout
        if (numLayoutNames > 0)
        {
            mLayoutsMenu->addSeparator();
        }

        // add the save current menu
        QAction* saveCurrentAction = mLayoutsMenu->addAction("Save Current");
        connect(saveCurrentAction, &QAction::triggered, this, &MainWindow::OnLayoutSaveAs);

        // remove menu is needed only if at least one layout
        if (numLayoutNames > 0)
        {
            // add the remove menu
            QMenu* removeMenu = mLayoutsMenu->addMenu("Remove");

            // add each layout in the remove menu
            for (uint32 i = 0; i < numLayoutNames; ++i)
            {
                QAction* action = removeMenu->addAction(mLayoutNames[i].c_str());
                connect(action, &QAction::triggered, this, &MainWindow::OnRemoveLayout);
            }
        }

        // disable signals to avoid to switch of layout
        mApplicationMode->blockSignals(true);

        // update the combo box
        mApplicationMode->clear();
        for (uint32 i = 0; i < numLayoutNames; ++i)
        {
            mApplicationMode->addItem(mLayoutNames[i].c_str());
        }

        // update the current selection of combo box
        const int layoutIndex = mApplicationMode->findText(QString(mOptions.GetApplicationMode().c_str()));
        mApplicationMode->setCurrentIndex(layoutIndex);

        // enable signals
        mApplicationMode->blockSignals(false);
    }


    // called when the application mode combo box changed
    void MainWindow::ApplicationModeChanged(const QString& text)
    {
        if (text.isEmpty())
        {
            // If the text is empty, this means no .layout files exist on disk.
            // In this case, load the built-in layout
            GetLayoutManager()->LoadLayout(":/EMotionFX/AnimGraph.layout");
            return;
        }

        // update the last used layout and save it in the preferences file
        mOptions.SetApplicationMode(text.toUtf8().data());
        SavePreferences();

        // generate the filename
        AZStd::string filename;
        filename = AZStd::string::format("%sLayouts/%s.layout", MysticQt::GetDataDir().c_str(), FromQtString(text).c_str());

        // try to load it
        if (GetLayoutManager()->LoadLayout(filename.c_str()) == false)
        {
            MCore::LogError("Failed to load layout from file '%s'", filename.c_str());
        }
    }


    // remove a given layout
    void MainWindow::OnRemoveLayout()
    {
        // make sure we really want to remove it
        QMessageBox msgBox(QMessageBox::Warning, "Remove The Selected Layout?", "Are you sure you want to remove the selected layout?<br>Note: This cannot be undone.", QMessageBox::Yes | QMessageBox::No, this);
        msgBox.setTextFormat(Qt::RichText);
        if (msgBox.exec() != QMessageBox::Yes)
        {
            return;
        }

        // generate the filename
        QAction* action = qobject_cast<QAction*>(sender());
        const QString filename = QString(MysticQt::GetDataDir().c_str()) + "Layouts/" + action->text() + ".layout";

        // try to remove the file
        QFile file(filename);
        if (file.remove() == false)
        {
            MCore::LogError("Failed to remove layout file '%s'", FromQtString(filename).c_str());
            return;
        }
        else
        {
            MCore::LogInfo("Successfullly removed layout file '%s'", FromQtString(filename).c_str());
        }

        // check if the layout removed is the current used
        if (QString(mOptions.GetApplicationMode().c_str()) == action->text())
        {
            // find the layout index on the application mode combo box
            const int layoutIndex = mApplicationMode->findText(action->text());

            // set the new layout index, take the previous if the last layout is removed, the next is taken otherwise
            const int newLayoutIndex = (layoutIndex == (mApplicationMode->count() - 1)) ? layoutIndex - 1 : layoutIndex + 1;

            // select the layout, it also keeps it and saves to config
            mApplicationMode->setCurrentIndex(newLayoutIndex);
        }

        // update the layouts menu
        UpdateLayoutsMenu();
    }


    // load a given layout
    void MainWindow::OnLoadLayout()
    {
        // get the menu action
        QAction* action = qobject_cast<QAction*>(sender());

        // update the last used layout and save it in the preferences file
        mOptions.SetApplicationMode(action->text().toUtf8().data());
        SavePreferences();

        // generate the filename
        AZStd::string filename;
        filename = AZStd::string::format("%sLayouts/%s.layout", MysticQt::GetDataDir().c_str(), FromQtString(action->text()).c_str());

        // try to load it
        if (GetLayoutManager()->LoadLayout(filename.c_str()))
        {
            // update the combo box
            mApplicationMode->blockSignals(true);
            const int layoutIndex = mApplicationMode->findText(action->text());
            mApplicationMode->setCurrentIndex(layoutIndex);
            mApplicationMode->blockSignals(false);
        }
        else
        {
            MCore::LogError("Failed to load layout from file '%s'", filename.c_str());
        }
    }


    // undo
    void MainWindow::OnUndo()
    {
        // check if we can undo
        if (GetCommandManager()->GetNumHistoryItems() > 0 && GetCommandManager()->GetHistoryIndex() >= 0)
        {
            // perform the undo
            AZStd::string outResult;
            const bool result = GetCommandManager()->Undo(outResult);

            // log the results if there are any
            if (outResult.size() > 0)
            {
                if (result == false)
                {
                    MCore::LogError(outResult.c_str());
                }
            }
        }

        // enable or disable the undo/redo menu options
        UpdateUndoRedo();
    }


    // redo
    void MainWindow::OnRedo()
    {
        // check if we can redo
        if (GetCommandManager()->GetNumHistoryItems() > 0 && GetCommandManager()->GetHistoryIndex() < (int32)GetCommandManager()->GetNumHistoryItems() - 1)
        {
            // perform the redo
            AZStd::string outResult;
            const bool result = GetCommandManager()->Redo(outResult);

            // log the results if there are any
            if (outResult.size() > 0)
            {
                if (result == false)
                {
                    MCore::LogError(outResult.c_str());
                }
            }
        }

        // enable or disable the undo/redo menu options
        UpdateUndoRedo();
    }


    // update the undo and redo status in the menu (disabled or enabled)
    void MainWindow::UpdateUndoRedo()
    {
        // check the undo status
        if (GetCommandManager()->GetNumHistoryItems() > 0 && GetCommandManager()->GetHistoryIndex() >= 0)
        {
            m_undoAction->setEnabled(true);
        }
        else
        {
            m_undoAction->setEnabled(false);
        }

        // check the redo status
        if (GetCommandManager()->GetNumHistoryItems() > 0 && GetCommandManager()->GetHistoryIndex() < (int32)GetCommandManager()->GetNumHistoryItems() - 1)
        {
            m_redoAction->setEnabled(true);
        }
        else
        {
            m_redoAction->setEnabled(false);
        }
    }


    // disable undo/redo
    void MainWindow::DisableUndoRedo()
    {
        m_undoAction->setEnabled(false);
        m_redoAction->setEnabled(false);
    }


    void MainWindow::LoadFile(const AZStd::string& fileName, int32 contextMenuPosX, int32 contextMenuPosY, bool contextMenuEnabled, bool reload)
    {
        AZStd::vector<AZStd::string> filenames;
        filenames.push_back(AZStd::string(fileName.c_str()));
        LoadFiles(filenames, contextMenuPosX, contextMenuPosY, contextMenuEnabled, reload);
    }


    void MainWindow::LoadFiles(const AZStd::vector<AZStd::string>& filenames, int32 contextMenuPosX, int32 contextMenuPosY, bool contextMenuEnabled, bool reload)
    {
        if (filenames.empty())
        {
            return;
        }

        AZStd::vector<AZStd::string> actorFilenames;
        AZStd::vector<AZStd::string> motionFilenames;
        AZStd::vector<AZStd::string> animGraphFilenames;
        AZStd::vector<AZStd::string> workspaceFilenames;
        AZStd::vector<AZStd::string> motionSetFilenames;

        // get the number of urls and iterate over them
        AZStd::string extension;
        for (const AZStd::string& filename : filenames)
        {
            // get the complete file name and extract the extension
            AzFramework::StringFunc::Path::GetExtension(filename.c_str(), extension, false /* include dot */);

            if (AzFramework::StringFunc::Equal(extension.c_str(), "actor"))
            {
                actorFilenames.push_back(filename);
            }
            else
            if (AzFramework::StringFunc::Equal(extension.c_str(), "motion"))
            {
                motionFilenames.push_back(filename);
            }
            else
            if (AzFramework::StringFunc::Equal(extension.c_str(), "animgraph"))
            {
                // Force-load from asset source folder.
                AZStd::string assetSourceFilename = filename;
                if (GetMainWindow()->GetFileManager()->RelocateToAssetSourceFolder(assetSourceFilename))
                {
                    animGraphFilenames.push_back(assetSourceFilename);
                }
            }
            else
            if (AzFramework::StringFunc::Equal(extension.c_str(), "emfxworkspace"))
            {
                workspaceFilenames.push_back(filename);
            }
            else
            if (AzFramework::StringFunc::Equal(extension.c_str(), "motionset"))
            {
                // Force-load from asset source folder.
                AZStd::string assetSourceFilename = filename;
                if (GetMainWindow()->GetFileManager()->RelocateToAssetSourceFolder(assetSourceFilename))
                {
                    motionSetFilenames.push_back(assetSourceFilename);
                }
            }
        }

        //--------------------

        if (!motionFilenames.empty())
        {
            CommandSystem::LoadMotionsCommand(motionFilenames, reload);
        }
        if (!motionSetFilenames.empty())
        {
            CommandSystem::LoadMotionSetsCommand(motionSetFilenames, reload, false);
        }

        CommandSystem::LoadAnimGraphsCommand(animGraphFilenames, reload);

        //--------------------

        const size_t actorCount = actorFilenames.size();
        if (actorCount == 1)
        {
            mDroppedActorFileName = actorFilenames[0].c_str();
            mRecentActors.AddRecentFile(mDroppedActorFileName.c_str());

            if (contextMenuEnabled)
            {
                if (EMotionFX::GetActorManager().GetNumActors() > 0)
                {
                    // create the drop context menu
                    QMenu menu(this);
                    QAction* openAction = menu.addAction("Open Actor");
                    QAction* mergeAction = menu.addAction("Merge Actor");
                    connect(openAction, &QAction::triggered, this, &MainWindow::OnOpenDroppedActor);
                    connect(mergeAction, &QAction::triggered, this, &MainWindow::OnMergeDroppedActor);

                    // show the menu at the given position
                    menu.exec(mapToGlobal(QPoint(contextMenuPosX, contextMenuPosY)));
                }
                else
                {
                    OnOpenDroppedActor();
                }
            }
            else
            {
                OnOpenDroppedActor();
            }
        }
        else
        {
            // Load and merge all actors.
            for (const AZStd::string& actorFilename : actorFilenames)
            {
                LoadActor(actorFilename.c_str(), false);
            }
        }

        //--------------------

        const size_t numWorkspaces = workspaceFilenames.size();
        if (numWorkspaces > 0)
        {
            // make sure we did not cancel load workspace
            if (mDirtyFileManager->SaveDirtyFiles() != DirtyFileManager::CANCELED)
            {
                // add the workspace in the recent workspace list
                // if the same workspace is already in the list, the duplicate is removed
                mRecentWorkspaces.AddRecentFile(workspaceFilenames[0]);

                // create the command group
                MCore::CommandGroup workspaceCommandGroup("Load workspace", 64);

                // clear everything before laoding a new workspace file
                Reset(true, true, true, true, &workspaceCommandGroup);
                workspaceCommandGroup.SetReturnFalseAfterError(true);

                // load the first workspace of the list as more doesn't make sense anyway
                Workspace* workspace = GetManager()->GetWorkspace();
                if (workspace->Load(workspaceFilenames[0].c_str(), &workspaceCommandGroup))
                {
                    // execute the group command
                    AZStd::string result;
                    if (GetCommandManager()->ExecuteCommandGroup(workspaceCommandGroup, result))
                    {
                        // set the workspace not dirty
                        workspace->SetDirtyFlag(false);

                        // for all registered plugins, call the after load workspace callback
                        PluginManager* pluginManager = GetPluginManager();
                        const uint32 numPlugins = pluginManager->GetNumActivePlugins();
                        for (uint32 p = 0; p < numPlugins; ++p)
                        {
                            EMStudioPlugin* plugin = pluginManager->GetActivePlugin(p);
                            plugin->OnAfterLoadProject();
                        }

                        // clear the history
                        GetCommandManager()->ClearHistory();

                        // set the window title using the workspace filename
                        SetWindowTitleFromFileName(workspaceFilenames[0].c_str());
                    }
                    else
                    {
                        AZ_Error("EMotionFX", false, result.c_str());
                    }
                }
            }
        }
    }

    void MainWindow::Activate(const AZ::Data::AssetId& actorAssetId, const EMotionFX::AnimGraph* animGraph, const EMotionFX::MotionSet* motionSet)
    {
        AZStd::string cachePath = gEnv->pFileIO->GetAlias("@assets@");
        AZStd::string filename;
        AzFramework::StringFunc::AssetDatabasePath::Normalize(cachePath);

        AZStd::string actorFilename;
        EBUS_EVENT_RESULT(actorFilename, AZ::Data::AssetCatalogRequestBus, GetAssetPathById, actorAssetId);
        AzFramework::StringFunc::AssetDatabasePath::Join(cachePath.c_str(), actorFilename.c_str(), filename, true);
        actorFilename = filename;

        MCore::CommandGroup commandGroup("Animgraph and motion set activation");
        AZStd::string commandString;

        const uint32 numActorInstances = EMotionFX::GetActorManager().GetNumActorInstances();
        for (uint32 i = 0; i < numActorInstances; ++i)
        {
            EMotionFX::ActorInstance* actorInstance = EMotionFX::GetActorManager().GetActorInstance(i);
            if (!actorInstance || actorFilename != actorInstance->GetActor()->GetFileName())
            {
                continue;
            }

            commandString = AZStd::string::format("ActivateAnimGraph -actorInstanceID %d -animGraphID %d -motionSetID %d",
                    actorInstance->GetID(),
                    animGraph->GetID(),
                    motionSet->GetID());
            commandGroup.AddCommandString(commandString);
        }

        AZStd::string result;
        if (!GetCommandManager()->ExecuteCommandGroup(commandGroup, result))
        {
            AZ_Error("EMotionFX", false, result.c_str());
        }
    }

    void MainWindow::LoadLayoutAfterShow()
    {
        if (!mLayoutLoaded)
        {
            mLayoutLoaded = true;

            LoadDefaultLayout();
            if (mCharacterFiles.empty() && GetManager()->GetAutoLoadLastWorkspace())
            {
                // load last workspace
                const AZStd::string lastRecentWorkspace = mRecentWorkspaces.GetLastRecentFileName();
                if (!lastRecentWorkspace.empty())
                {
                    mCharacterFiles.push_back(lastRecentWorkspace);
                }
            }
            if (!mCharacterFiles.empty())
            {
                // Need to defer loading the character until the layout is ready. We also
                // need a couple of initializeGL/paintGL to happen before the character
                // is being loaded.
                QTimer::singleShot(1000, this, &MainWindow::LoadCharacterFiles);
            }
        }
    }

    void MainWindow::RaiseFloatingWidgets()
    {
        const QList<MysticQt::DockWidget*> dockWidgetList = findChildren<MysticQt::DockWidget*>();
        for (MysticQt::DockWidget* dockWidget : dockWidgetList)
        {
            if (dockWidget->isFloating())
            {
                // There is some weird behavior with floating QDockWidget. After showing it,
                // the widget doesn't seem to remain when we move/maximize or do some changes in the
                // window that contains it. Setting it as floating false then true seems to workaround
                // the problem
                dockWidget->setFloating(false);
                dockWidget->setFloating(true);

                dockWidget->show();
                dockWidget->raise();
            }
        }
    }

    // Load default layout.
    void MainWindow::LoadDefaultLayout()
    {
        if (mApplicationMode->count() == 0)
        {
            // When the combo box is empty, the call to setCurrentIndex will
            // not cause any slots to be fired, so dispatch the call manually.
            // Pass an empty string to duplicate the behavior of calling
            // currentText() on an empty combo box
            ApplicationModeChanged("");
            return;
        }

        int layoutIndex = mApplicationMode->findText(mOptions.GetApplicationMode().c_str());

        // If searching for the last used layout fails load the default or viewer layout if they exist
        if (layoutIndex == -1)
        {
            layoutIndex = mApplicationMode->findText("AnimGraph");
        }
        if (layoutIndex == -1)
        {
            layoutIndex = mApplicationMode->findText("Character");
        }
        if (layoutIndex == -1)
        {
            layoutIndex = mApplicationMode->findText("Animation");
        }

        mApplicationMode->setCurrentIndex(layoutIndex);
    }


    EMotionFX::ActorInstance* MainWindow::GetSelectedActorInstance()
    {
        return GetCommandManager()->GetCurrentSelection().GetSingleActorInstance();
    }


    EMotionFX::Actor* MainWindow::GetSelectedActor()
    {
        return GetCommandManager()->GetCurrentSelection().GetSingleActor();
    }


    void MainWindow::BroadcastSelectionNotifications()
    {
        const CommandSystem::SelectionList& selectionList = GetCommandManager()->GetCurrentSelection();

        // Handle actor selection changes.
        EMotionFX::Actor* selectedActor = selectionList.GetSingleActor();
        if (m_prevSelectedActor != selectedActor)
        {
            EMotionFX::ActorEditorNotificationBus::Broadcast(&EMotionFX::ActorEditorNotifications::ActorSelectionChanged, selectedActor);
        }
        m_prevSelectedActor = selectedActor;

        // Handle actor instance selection changes.
        EMotionFX::ActorInstance* selectedActorInstance = selectionList.GetSingleActorInstance();
        if (m_prevSelectedActorInstance != selectedActorInstance)
        {
            EMotionFX::ActorEditorNotificationBus::Broadcast(&EMotionFX::ActorEditorNotifications::ActorInstanceSelectionChanged, selectedActorInstance);
        }
        m_prevSelectedActorInstance = selectedActorInstance;
    }


    void MainWindow::LoadCharacterFiles()
    {
        if (!mCharacterFiles.empty())
        {
            LoadFiles(mCharacterFiles, 0, 0, false, true);
            mCharacterFiles.clear();

            // for all registered plugins, call the after load actors callback
            PluginManager* pluginManager = GetPluginManager();
            const uint32 numPlugins = pluginManager->GetNumActivePlugins();
            for (uint32 p = 0; p < numPlugins; ++p)
            {
                EMStudioPlugin* plugin = pluginManager->GetActivePlugin(p);
                plugin->OnAfterLoadActors();
            }
        }
    }


    // accept drops
    void MainWindow::dragEnterEvent(QDragEnterEvent* event)
    {
        // this is needed to actually reach the drop event function
        event->acceptProposedAction();
    }


    // gets called when the user drag&dropped an actor to the application and then chose to open it in the context menu
    void MainWindow::OnOpenDroppedActor()
    {
        if (mDirtyFileManager->SaveDirtyFiles({azrtti_typeid<EMotionFX::Actor>()}) == DirtyFileManager::CANCELED)
        {
            return;
        }
        LoadActor(mDroppedActorFileName.c_str(), true);
    }


    // gets called when the user drag&dropped an actor to the application and then chose to merge it in the context menu
    void MainWindow::OnMergeDroppedActor()
    {
        LoadActor(mDroppedActorFileName.c_str(), false);
    }


    // handle drop events
    void MainWindow::dropEvent(QDropEvent* event)
    {
        // check if we dropped any files to the application
        const QMimeData* mimeData = event->mimeData();

        AZStd::vector<AzToolsFramework::AssetBrowser::AssetBrowserEntry*> entries;
        AzToolsFramework::AssetBrowser::AssetBrowserEntry::FromMimeData(mimeData, entries);

        AZStd::vector<AZStd::string> fileNames;
        for (const auto& entry : entries)
        {
            AZStd::vector<const AzToolsFramework::AssetBrowser::ProductAssetBrowserEntry*> productEntries;
            entry->GetChildrenRecursively<AzToolsFramework::AssetBrowser::ProductAssetBrowserEntry>(productEntries);
            for (const auto& productEntry : productEntries)
            {
                fileNames.emplace_back(FileManager::GetAssetFilenameFromAssetId(productEntry->GetAssetId()));
            }
        }
        LoadFiles(fileNames, event->pos().x(), event->pos().y());

        event->acceptProposedAction();
    }


    void MainWindow::closeEvent(QCloseEvent* event)
    {
        if (mDirtyFileManager->SaveDirtyFiles() == DirtyFileManager::CANCELED)
        {
            event->ignore();
        }
        else
        {
            mAutosaveTimer->stop();

            PluginManager* pluginManager = GetPluginManager();

            // The close event does not hide floating widgets, so we are doing that manually here
            const QList<MysticQt::DockWidget*> dockWidgetList = findChildren<MysticQt::DockWidget*>();
            for (MysticQt::DockWidget* dockWidget : dockWidgetList)
            {
                if (dockWidget->isFloating())
                {
                    dockWidget->hide();
                }
            }

            // get a copy of the active plugins since some plugins may choose
            // to get inactive when the main window closes
            const AZStd::vector<EMStudioPlugin*> activePlugins = pluginManager->GetActivePlugins();
            for (EMStudioPlugin* activePlugin : activePlugins)
            {
                AZ_Assert(activePlugin, "Unexpected null active plugin");
                activePlugin->OnMainWindowClosed();
            }

            QMainWindow::closeEvent(event);
        }

        // We mark it as false so next time is shown the layout is re-loaded if
        // necessary
        mLayoutLoaded = false;
    }


    void MainWindow::showEvent(QShowEvent* event)
    {
        if (mOptions.GetEnableAutoSave())
        {
            mAutosaveTimer->setInterval(mOptions.GetAutoSaveInterval() * 60 * 1000);
            mAutosaveTimer->start();
        }

        // EMotionFX dock widget is created the first time it's opened, so we need to load layout after that
        // The singleShot is needed because show event is fired before the dock widget resizes (in the same function dock widget is created)
        // So we want to load layout after that. It's a bit hacky, but most sensible at the moment.
        if (!mLayoutLoaded)
        {
            QTimer::singleShot(0, this, &MainWindow::LoadLayoutAfterShow);
        }

        QMainWindow::showEvent(event);

        // This show event ends up being called twice from QtViewPaneManager::OpenPane. At the end of the method
        // is doing a "raise" on this window. Since we cannot intercept that raise (raise is a slot and doesn't
        // have an event associated) we are deferring a call to RaiseFloatingWidgets which will raise the floating
        // widgets (this needs to happen after the raise from OpenPane).
        QTimer::singleShot(0, this, &MainWindow::RaiseFloatingWidgets);
    }

    void MainWindow::keyPressEvent(QKeyEvent* event)
    {
        const char* layoutGroupName = "Layouts";
        const uint32 numLayouts = GetMainWindow()->GetNumLayouts();
        for (uint32 i = 0; i < numLayouts; ++i)
        {
            if (mShortcutManager->Check(event, GetLayoutName(i), layoutGroupName))
            {
                mApplicationMode->setCurrentIndex(i);
                event->accept();
                return;
            }
        }

        event->ignore();
    }


    void MainWindow::keyReleaseEvent(QKeyEvent* event)
    {
        const char* layoutGroupName = "Layouts";
        const uint32 numLayouts = GetNumLayouts();
        for (uint32 i = 0; i < numLayouts; ++i)
        {
            if (mShortcutManager->Check(event, layoutGroupName, GetLayoutName(i)))
            {
                event->accept();
                return;
            }
        }

        event->ignore();
    }


    // get the name of the currently active layout
    const char* MainWindow::GetCurrentLayoutName() const
    {
        // get the selected layout
        const int currentLayoutIndex = mApplicationMode->currentIndex();

        // if the index is out of range, return empty name
        if ((currentLayoutIndex < 0) || (currentLayoutIndex >= (int32)GetNumLayouts()))
        {
            return "";
        }

        // return the layout name
        return GetLayoutName(currentLayoutIndex);
    }

    const char* MainWindow::GetEMotionFXPaneName()
    {
        return LyViewPane::AnimationEditor;
    }


    void MainWindow::OnAutosaveTimeOut()
    {
        AZStd::vector<AZStd::string> filenames;
        AZStd::vector<AZStd::string> dirtyFileNames;
        AZStd::vector<SaveDirtyFilesCallback::ObjectPointer> objects;
        AZStd::vector<SaveDirtyFilesCallback::ObjectPointer> dirtyObjects;

        const size_t numDirtyFilesCallbacks = mDirtyFileManager->GetNumCallbacks();
        for (size_t i = 0; i < numDirtyFilesCallbacks; ++i)
        {
            SaveDirtyFilesCallback* callback = mDirtyFileManager->GetCallback(i);
            callback->GetDirtyFileNames(&filenames, &objects);
            const size_t numFileNames = filenames.size();
            for (size_t j = 0; j < numFileNames; ++j)
            {
                // bypass if the filename is empty
                // it's the case when the file is not already saved
                if (filenames[j].empty())
                {
                    continue;
                }

                // avoid duplicate of filename and object
                if (AZStd::find(dirtyFileNames.begin(), dirtyFileNames.end(), filenames[j]) == dirtyFileNames.end())
                {
                    dirtyFileNames.push_back(filenames[j]);
                    dirtyObjects.push_back(objects[j]);
                }
            }
        }

        // Skip directly in case there are no dirty files.
        if (dirtyFileNames.empty() && dirtyObjects.empty())
        {
            return;
        }

        // create the command group
        AZStd::string command;
        MCore::CommandGroup commandGroup("Autosave");

        // get the autosaves folder
        const AZStd::string autosavesFolder = GetManager()->GetAutosavesFolder();

        // save each dirty object
        QStringList entryList;
        AZStd::string filename, extension, startWithAutosave;
        const size_t numFileNames = dirtyFileNames.size();
        for (size_t i = 0; i < numFileNames; ++i)
        {
            // get the full path
            filename = dirtyFileNames[i];

            // get the base name with autosave
            AzFramework::StringFunc::Path::GetFileName(filename.c_str(), startWithAutosave);
            startWithAutosave += "_Autosave";

            // get the extension
            AzFramework::StringFunc::Path::GetExtension(filename.c_str(), extension, false /* include dot */);

            // open the dir and get the file list
            const QDir dir = QDir(autosavesFolder.c_str());
            entryList = dir.entryList(QDir::Files, QDir::Time | QDir::Reversed);

            // generate the autosave file list
            int maxAutosaveFileNumber = 0;
            QList<QString> autosaveFileList;
            const int numEntry = entryList.size();
            for (int j = 0; j < numEntry; ++j)
            {
                // get the file info
                const QFileInfo fileInfo = QFileInfo(QString::fromStdString(autosavesFolder.data()) + entryList[j]);

                // check the extension
                if (fileInfo.suffix() != extension.c_str())
                {
                    continue;
                }

                // check the base name
                const QString baseName = fileInfo.baseName();
                if (baseName.startsWith(startWithAutosave.c_str()))
                {
                    // extract the number
                    const int numberExtracted = baseName.mid(static_cast<int>(startWithAutosave.size())).toInt();

                    // check if the number is valid
                    if (numberExtracted > 0)
                    {
                        // add the file in the list
                        autosaveFileList.append(QString::fromStdString(autosavesFolder.data()) + entryList[j]);
                        AZ_Printf("EMotionFX", "Appending '%s' #%i\n", entryList[j].toUtf8().data(), numberExtracted);

                        // Update the maximum autosave file number that already exists on disk.
                        maxAutosaveFileNumber = MCore::Max<int>(maxAutosaveFileNumber, numberExtracted);
                    }
                }
            }

            // check if the length is upper than the max num files
            if (autosaveFileList.length() >= mOptions.GetAutoSaveNumberOfFiles())
            {
                // number of files to delete
                // one is added because one space needs to be free for the new file
                const int numFilesToDelete = mOptions.GetAutoSaveNumberOfFiles() ? (autosaveFileList.size() - mOptions.GetAutoSaveNumberOfFiles() + 1) : autosaveFileList.size();

                // delete each file
                for (int j = 0; j < numFilesToDelete; ++j)
                {
                    AZ_Printf("EMotionFX", "Removing '%s'\n", autosaveFileList[j].toUtf8().data());
                    QFile::remove(autosaveFileList[j]);
                }
            }

            // Set the new autosave file number and prevent an integer overflow.
            int newAutosaveFileNumber = maxAutosaveFileNumber + 1;
            if (newAutosaveFileNumber == std::numeric_limits<int>::max())
            {
                // Restart counting autosave file numbers from the beginning.
                newAutosaveFileNumber = 1;
            }

            // save the new file
            AZStd::string newFileFilename;
            newFileFilename = AZStd::string::format("%s%s%d.%s", autosavesFolder.c_str(), startWithAutosave.c_str(), newAutosaveFileNumber, extension.c_str());
            AZ_Printf("EMotionFX", "Saving to '%s'\n", newFileFilename.c_str());

            // Backing up actors and motions doesn't work anymore as we just update the .assetinfos and the asset processor does the rest.
            if (dirtyObjects[i].mMotionSet)
            {
                command = AZStd::string::format("SaveMotionSet -motionSetID %i -filename \"%s\" -updateFilename false -updateDirtyFlag false -sourceControl false", dirtyObjects[i].mMotionSet->GetID(), newFileFilename.c_str());
                commandGroup.AddCommandString(command);
            }
            else if (dirtyObjects[i].mAnimGraph)
            {
                const uint32 animGraphIndex = EMotionFX::GetAnimGraphManager().FindAnimGraphIndex(dirtyObjects[i].mAnimGraph);
                command = AZStd::string::format("SaveAnimGraph -index %i -filename \"%s\" -updateFilename false -updateDirtyFlag false -sourceControl false", animGraphIndex, newFileFilename.c_str());
                commandGroup.AddCommandString(command);
            }
            else if (dirtyObjects[i].mWorkspace)
            {
                Workspace* workspace = GetManager()->GetWorkspace();
                workspace->Save(newFileFilename.c_str(), false, false);
            }
        }

        // execute the command group
        AZStd::string result;
        if (GetCommandManager()->ExecuteCommandGroup(commandGroup, result, false))
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_SUCCESS, 
                "Autosave <font color=green>completed</font>");
        }
        else
        {
            GetNotificationWindowManager()->CreateNotificationWindow(NotificationWindow::TYPE_ERROR, 
                AZStd::string::format("Autosave <font color=red>failed</font><br/><br/>%s", result.c_str()).c_str());
        }
    }

    
    void MainWindow::moveEvent(QMoveEvent* event)
    {
        MCORE_UNUSED(event);
        GetManager()->GetNotificationWindowManager()->OnMovedOrResized();
    }


    void MainWindow::resizeEvent(QResizeEvent* event)
    {
        MCORE_UNUSED(event);
        GetManager()->GetNotificationWindowManager()->OnMovedOrResized();
    }


    void MainWindow::OnUpdateRenderPlugins()
    {
        // sort the active plugins based on their priority
        PluginManager* pluginManager = GetPluginManager();

        // get the number of active plugins, iterate through them and call the process frame method
        const uint32 numPlugins = pluginManager->GetNumActivePlugins();
        for (uint32 p = 0; p < numPlugins; ++p)
        {
            EMStudioPlugin* plugin = pluginManager->GetActivePlugin(p);
            if (plugin->GetPluginType() == EMStudioPlugin::PLUGINTYPE_RENDERING)
            {
                plugin->ProcessFrame(0.0f);
            }
        }
    }

} // namespace EMStudio

#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/MainWindow.moc>
