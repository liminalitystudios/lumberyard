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

#pragma once

#include <EMotionStudio/EMStudioSDK/Source/EMStudioConfig.h>
#include <EMotionStudio/EMStudioSDK/Source/GUIOptions.h>
#include <EMotionStudio/EMStudioSDK/Source/PluginOptionsBus.h>
#include <MCore/Source/Array.h>
#include <MCore/Source/Command.h>
#include <MCore/Source/StandardHeaders.h>
#include <MCore/Source/CommandManagerCallback.h>
#include <MysticQt/Source/RecentFiles.h>
#include <Editor/ActorEditorBus.h>

#include <QMainWindow>
#include <QAbstractNativeEventFilter>

// forward declarations
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QMenu)
QT_FORWARD_DECLARE_CLASS(QTimer)
QT_FORWARD_DECLARE_CLASS(QDropEvent)
QT_FORWARD_DECLARE_CLASS(QCheckBox)
struct SelectionItem;

namespace AZ
{
    namespace Data
    {
        struct AssetId;
    }
}

namespace EMotionFX
{
    class AnimGraph;
    class MotionSet;
}

namespace MCore
{
    class CommandGroup;
}

namespace MysticQt
{
    class ComboBox;
    class KeyboardShortcutManager;
}

namespace EMStudio
{
    // forward declaration
    class DirtyFileManager;
    class EMStudioPlugin;
    class FileManager;
    class MainWindow;
    class NativeEventFilter;
    class NodeSelectionWindow;
    class PreferencesWindow;
    class UndoMenuCallback;

    // the main window
    class EMSTUDIO_API MainWindow
        : public QMainWindow
        , private PluginOptionsNotificationsBus::Router
        , public EMotionFX::ActorEditorRequestBus::Handler
    {
        Q_OBJECT
        MCORE_MEMORYOBJECTCATEGORY(MainWindow, MCore::MCORE_DEFAULT_ALIGNMENT, MEMCATEGORY_EMSTUDIOSDK)

    public:
        MainWindow(QWidget* parent = nullptr, Qt::WindowFlags flags = nullptr);
        ~MainWindow();

        void UpdateCreateWindowMenu();
        void UpdateLayoutsMenu();
        void UpdateUndoRedo();
        void DisableUndoRedo();
        static void Reflect(AZ::ReflectContext* context);
        void Init();

        MCORE_INLINE QMenu* GetLayoutsMenu()                                    { return mLayoutsMenu; }

        void LoadActor(const char* fileName, bool replaceCurrentScene);
        void LoadCharacter(const AZ::Data::AssetId& actorAssetId, const AZ::Data::AssetId& animgraphId, const AZ::Data::AssetId& motionSetId);
        void LoadFile(const AZStd::string& fileName, int32 contextMenuPosX = 0, int32 contextMenuPosY = 0, bool contextMenuEnabled = true, bool reload = false);
        void LoadFiles(const AZStd::vector<AZStd::string>& filenames, int32 contextMenuPosX = 0, int32 contextMenuPosY = 0, bool contextMenuEnabled = true, bool reload = false);

        void Activate(const AZ::Data::AssetId& actorAssetId, const EMotionFX::AnimGraph* animGraph, const EMotionFX::MotionSet* motionSet);

        MysticQt::RecentFiles* GetRecentWorkspaces()                            { return &mRecentWorkspaces; }

        GUIOptions& GetOptions()                                                { return mOptions; }

        void Reset(bool clearActors = true, bool clearMotionSets = true, bool clearMotions = true, bool clearAnimGraphs = true, MCore::CommandGroup* commandGroup = nullptr);

        // settings
        void SavePreferences();
        void LoadPreferences();

        void UpdateResetAndSaveAllMenus();

        void UpdateSaveActorsMenu();
        void EnableMergeActorMenu();
        void DisableMergeActorMenu();
        void EnableSaveSelectedActorsMenu();
        void DisableSaveSelectedActorsMenu();

        void OnWorkspaceSaved(const char* filename);

        MCORE_INLINE MysticQt::ComboBox* GetApplicationModeComboBox()           { return mApplicationMode; }
        DirtyFileManager*   GetDirtyFileManager() const                         { return mDirtyFileManager; }
        FileManager*        GetFileManager() const                              { return mFileManager; }
        PreferencesWindow*  GetPreferencesWindow() const                        { return mPreferencesWindow; }

        void keyPressEvent(QKeyEvent* event) override;
        void keyReleaseEvent(QKeyEvent* event) override;

        uint32 GetNumLayouts() const                                            { return mLayoutNames.GetLength(); }
        const char* GetLayoutName(uint32 index) const                           { return mLayoutNames[index].c_str(); }
        const char* GetCurrentLayoutName() const;

        static const char* GetEMotionFXPaneName();
        MysticQt::KeyboardShortcutManager* GetShortcutManager() const           { return mShortcutManager; }

    public slots:
        void OnAutosaveTimeOut();
        void LoadLayoutAfterShow();
        void RaiseFloatingWidgets();
        void LoadCharacterFiles();

    protected:
        void moveEvent(QMoveEvent* event) override;
        void resizeEvent(QResizeEvent* event) override;
        void LoadDefaultLayout();

    private:
        // ActorEditorRequests
        EMotionFX::ActorInstance* GetSelectedActorInstance() override;
        EMotionFX::Actor* GetSelectedActor() override;

        void BroadcastSelectionNotifications();
        EMotionFX::Actor*           m_prevSelectedActor;
        EMotionFX::ActorInstance*   m_prevSelectedActorInstance;

        QMenu*                  mCreateWindowMenu;
        QMenu*                  mLayoutsMenu;
        QAction*                m_undoAction;
        QAction*                m_redoAction;

        // keyboard shortcut manager
        MysticQt::KeyboardShortcutManager* mShortcutManager;

        // layouts (application modes)
        MCore::Array<AZStd::string> mLayoutNames;
        bool mLayoutLoaded;

        // menu actions
        QAction*                mResetAction;
        QAction*                mSaveAllAction;
        QAction*                mMergeActorAction;
        QAction*                mSaveSelectedActorsAction;
#ifdef EMFX_DEVELOPMENT_BUILD
        QAction*                mSaveSelectedActorAsAttachmentsAction;
#endif

        // application mode
        MysticQt::ComboBox*     mApplicationMode;

        PreferencesWindow*      mPreferencesWindow;

        FileManager*  mFileManager;

        MysticQt::RecentFiles   mRecentActors;
        MysticQt::RecentFiles   mRecentWorkspaces;

        // dirty files
        DirtyFileManager*       mDirtyFileManager;

        void SetWindowTitleFromFileName(const AZStd::string& fileName);

        // drag & drop support
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dropEvent(QDropEvent* event) override;
        AZStd::string           mDroppedActorFileName;

        // General options
        GUIOptions                              mOptions;
        bool                                    mLoadingOptions;
        
        QTimer*                                 mAutosaveTimer;
        
        AZStd::vector<AZStd::string>            mCharacterFiles;

        NativeEventFilter*                      mNativeEventFilter;

        void closeEvent(QCloseEvent* event) override;
        void showEvent(QShowEvent* event) override;

        void OnOptionChanged(const AZStd::string& optionChanged) override;

        UndoMenuCallback*                       m_undoMenuCallback;

        // declare the callbacks
        MCORE_DEFINECOMMANDCALLBACK(CommandImportActorCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandRemoveActorCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandRemoveActorInstanceCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandImportMotionCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandRemoveMotionCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandCreateMotionSetCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandRemoveMotionSetCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandLoadMotionSetCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandCreateAnimGraphCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandRemoveAnimGraphCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandLoadAnimGraphCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandSelectCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandUnselectCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandClearSelectionCallback);
        MCORE_DEFINECOMMANDCALLBACK(CommandSaveWorkspaceCallback);
        CommandImportActorCallback*         mImportActorCallback;
        CommandRemoveActorCallback*         mRemoveActorCallback;
        CommandRemoveActorInstanceCallback* mRemoveActorInstanceCallback;
        CommandImportMotionCallback*        mImportMotionCallback;
        CommandRemoveMotionCallback*        mRemoveMotionCallback;
        CommandCreateMotionSetCallback*     mCreateMotionSetCallback;
        CommandRemoveMotionSetCallback*     mRemoveMotionSetCallback;
        CommandLoadMotionSetCallback*       mLoadMotionSetCallback;
        CommandCreateAnimGraphCallback*     mCreateAnimGraphCallback;
        CommandRemoveAnimGraphCallback*     mRemoveAnimGraphCallback;
        CommandLoadAnimGraphCallback*       mLoadAnimGraphCallback;
        CommandSelectCallback*              mSelectCallback;
        CommandUnselectCallback*            mUnselectCallback;
        CommandClearSelectionCallback*      m_clearSelectionCallback;
        CommandSaveWorkspaceCallback*       mSaveWorkspaceCallback;

        class MainWindowCommandManagerCallback : public MCore::CommandManagerCallback
        {
            //////////////////////////////////////////////////////////////////////////////////////
            /// CommandManagerCallback implementation
            void OnPreExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine) override;
            void OnPostExecuteCommand(MCore::CommandGroup* /*group*/, MCore::Command* /*command*/, const MCore::CommandLine& /*commandLine*/, bool /*wasSuccess*/, const AZStd::string& /*outResult*/) override { }
            void OnPreUndoCommand(MCore::Command* command, const MCore::CommandLine& commandLine);
            void OnPreExecuteCommandGroup(MCore::CommandGroup* /*group*/, bool /*undo*/) override { }
            void OnPostExecuteCommandGroup(MCore::CommandGroup* /*group*/, bool /*wasSuccess*/) override { }
            void OnAddCommandToHistory(uint32 /*historyIndex*/, MCore::CommandGroup* /*group*/, MCore::Command* /*command*/, const MCore::CommandLine& /*commandLine*/) override { }
            void OnRemoveCommand(uint32 /*historyIndex*/) override { }
            void OnSetCurrentCommand(uint32 /*index*/) override { }
        };

        MainWindowCommandManagerCallback m_mainWindowCommandManagerCallback;

    public slots:
        void OnFileOpenActor();
        void OnFileSaveSelectedActors();
        void OnReset();
        void OnFileMergeActor();
        void OnOpenDroppedActor();
        void OnRecentFile(QAction* action);
        void OnMergeDroppedActor();
        void OnFileNewWorkspace();
        void OnFileOpenWorkspace();
        void OnFileSaveWorkspace();
        void OnFileSaveWorkspaceAs();
        void OnWindowCreate(bool checked);
        void OnLayoutSaveAs();
        void OnRemoveLayout();
        void OnLoadLayout();
        void OnUndo();
        void OnRedo();
        void OnOpenAutosaveFolder();
        void OnOpenSettingsFolder();
        void OnPreferences();
        void OnSaveAll();
        void ApplicationModeChanged(const QString& text);
        void OnUpdateRenderPlugins();

     signals:
        void HardwareChangeDetected();
    };

} // namespace EMStudio
