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

#include "EMStudioManager.h"
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/Serialization/SerializeContext.h>
#include "RecoverFilesWindow.h"
#include "MotionEventPresetManager.h"
#include <EMotionFX/Tools/EMotionStudio/EMStudioSDK/Source/Commands.h>
#include <EMotionStudio/EMStudioSDK/Source/Allocators.h>

// include MCore related
#include <MCore/Source/LogManager.h>
#include <MCore/Source/CommandManager.h>
#include <MCore/Source/FileSystem.h>
#include <EMotionFX/Source/AnimGraphManager.h>
#include <EMotionFX/Source/EMotionFXManager.h>
#include <EMotionFX/Source/MotionManager.h>

#include <Source/Editor/SkeletonModel.h>

// include Qt related things
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QUuid>
#include <QSplashScreen>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QStandardPaths>
#include <QPushButton>

// include AzCore required headers
#include <AzFramework/API/ApplicationAPI.h>

namespace EMStudio
{
    //--------------------------------------------------------------------------
    // globals
    //--------------------------------------------------------------------------
    EMStudioManager* gEMStudioMgr = nullptr;


    //--------------------------------------------------------------------------
    // class EMStudioManager
    //--------------------------------------------------------------------------

    // constructor
    EMStudioManager::EMStudioManager(QApplication* app, int& argc, char* argv[])
    {
        // Flag that we have an editor around
        EMotionFX::GetEMotionFX().SetIsInEditorMode(true);

        mHTMLLinkString.reserve(32768);
        mEventProcessingCallback = nullptr;
        mAutoLoadLastWorkspace = false;
        mAvoidRendering = false;

        mApp = app;

        {
            UIAllocator::Descriptor uiAllocatorDescriptor;
            uiAllocatorDescriptor.m_custom = &AZ::AllocatorInstance<AZ::SystemAllocator>::Get();
            AZ::AllocatorInstance<UIAllocator>::Create();
        }

        AZ::SerializeContext* serializeContext = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
        if (!serializeContext)
        {
            AZ_Error("EMotionFX", false, "Can't get serialize context from component application.");
        }
        else
        {
            MainWindow::Reflect(serializeContext);
        }


        // create and setup a log file
        MCore::GetLogManager().CreateLogFile(AZStd::string(GetAppDataFolder() + "EMStudioLog.txt").c_str());
        //#ifdef MCORE_DEBUG
        MCore::GetLogManager().SetLogLevels(MCore::LogCallback::LOGLEVEL_ALL);
        //#endif

        // Register editor specific commands.
        mCommandManager = new CommandSystem::CommandManager();
        mCommandManager->RegisterCommand(new CommandSaveActorAssetInfo());
        mCommandManager->RegisterCommand(new CommandSaveMotionAssetInfo());
        mCommandManager->RegisterCommand(new CommandSaveMotionSet());
        mCommandManager->RegisterCommand(new CommandSaveAnimGraph());
        mCommandManager->RegisterCommand(new CommandSaveWorkspace());

        mEventPresetManager         = new MotionEventPresetManager();
        mPluginManager              = new PluginManager();
        mLayoutManager              = new LayoutManager();
        mNotificationWindowManager  = new NotificationWindowManager();
        mCompileDate = AZStd::string::format("%s", MCORE_DATE);

        EMotionFX::SkeletonOutlinerNotificationBus::Handler::BusConnect();

        // log some information
        LogInfo();
    }


    // destructor
    EMStudioManager::~EMStudioManager()
    {
        EMotionFX::SkeletonOutlinerNotificationBus::Handler::BusDisconnect();

        if (mEventProcessingCallback)
        {
            EMStudio::GetCommandManager()->RemoveCallback(mEventProcessingCallback, false);
            delete mEventProcessingCallback;
        }

        // delete all animgraph instances etc
        ClearScene();

        delete mEventPresetManager;
        delete mPluginManager;
        delete mLayoutManager;
        delete mNotificationWindowManager;
        delete mMainWindow;
        delete mCommandManager;

        AZ::AllocatorInstance<UIAllocator>::Destroy();
    }

    MainWindow* EMStudioManager::GetMainWindow()
    {
        if (mMainWindow.isNull())
        {
            mMainWindow = new MainWindow();
            mMainWindow->Init();
        }
        return mMainWindow;
    }


    // clear everything
    void EMStudioManager::ClearScene()
    {
        GetMainWindow()->Reset();
        EMotionFX::GetAnimGraphManager().RemoveAllAnimGraphInstances(true);
        EMotionFX::GetAnimGraphManager().RemoveAllAnimGraphs(true);
        EMotionFX::GetMotionManager().Clear(true);
    }


    // log info
    void EMStudioManager::LogInfo()
    {
        MCore::LogInfo("-----------------------------------------------");
        MCore::LogInfo("EMotion Studio Core - Information");
        MCore::LogInfo("-----------------------------------------------");
        MCore::LogInfo("Compilation date: %s", GetCompileDate());
        MCore::LogInfo("-----------------------------------------------");
    }


    int EMStudioManager::ExecuteApp()
    {
        MCORE_ASSERT(mApp);
        MCORE_ASSERT(mMainWindow);

#if !defined(EMFX_EMSTUDIOLYEMBEDDED)
        // try to load all plugins
        AZStd::string pluginDir = MysticQt::GetAppDir() + "Plugins/";

        mPluginManager->LoadPluginsFromDirectory(pluginDir.c_str());
#endif // EMFX_EMSTUDIOLYEMBEDDED

        // Give a chance to every plugin to reflect data
        const uint32 numPlugins = mPluginManager->GetNumPlugins();
        if (numPlugins)
        {
            AZ::SerializeContext* serializeContext = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(serializeContext, &AZ::ComponentApplicationBus::Events::GetSerializeContext);
            if (!serializeContext)
            {
                AZ_Error("EMotionFX", false, "Can't get serialize context from component application.");
            }
            else
            {
                for (uint32 i = 0; i < numPlugins; ++i)
                {
                    EMStudioPlugin* plugin = mPluginManager->GetPlugin(i);
                    plugin->Reflect(serializeContext);
                }
            }
        }
        
        // Register the command event processing callback.
        mEventProcessingCallback = new EventProcessingCallback();
        EMStudio::GetCommandManager()->RegisterCallback(mEventProcessingCallback);

        // Update the main window create window item with, so that it shows all loaded plugins.
        mMainWindow->UpdateCreateWindowMenu();

        // Set the recover save path.
        MCore::FileSystem::mSecureSavePath = GetManager()->GetRecoverFolder().c_str();

        // Show the main dialog and wait until it closes.
        MCore::LogInfo("EMotion Studio initialized...");

#if !defined(EMFX_EMSTUDIOLYEMBEDDED)
        mMainWindow->show();
#endif // EMFX_EMSTUDIOLYEMBEDDED

        // Show the recover window in case we have some .recover files in the recovery folder.
        const QString secureSavePath = MCore::FileSystem::mSecureSavePath.c_str();
        const QStringList recoverFileList = QDir(secureSavePath).entryList(QStringList("*.recover"), QDir::Files);
        if (!recoverFileList.empty())
        {
            // Add each recover file to the array.
            const size_t numRecoverFiles = recoverFileList.size();
            AZStd::vector<AZStd::string> recoverStringArray;
            recoverStringArray.reserve(numRecoverFiles);
            AZStd::string recoverFilename, backupFilename;
            for (int i = 0; i < numRecoverFiles; ++i)
            {
                recoverFilename = QString(secureSavePath + recoverFileList[i]).toUtf8().data();

                backupFilename = recoverFilename;
                AzFramework::StringFunc::Path::StripExtension(backupFilename);

                // Add the recover filename only if the backup file exists.
                if (AZ::IO::FileIOBase::GetInstance()->Exists(backupFilename.c_str()))
                {
                    recoverStringArray.push_back(recoverFilename);
                }
            }

            // Show the recover files window only in case there is a valid file to recover.
            if (!recoverStringArray.empty())
            {
                RecoverFilesWindow* recoverFilesWindow = new RecoverFilesWindow(mMainWindow, recoverStringArray);
                recoverFilesWindow->exec();
            }
        }

        mApp->processEvents();

#if !defined(EMFX_EMSTUDIOLYEMBEDDED)
        // execute the application
        return mApp->exec();
#else
        return 0;
#endif // EMFX_EMSTUDIOLYEMBEDDED
    }

    void EMStudioManager::SetWidgetAsInvalidInput(QWidget* widget)
    {
        widget->setStyleSheet("border: 1px solid red;");
    }


    const char* EMStudioManager::ConstructHTMLLink(const char* text, const MCore::RGBAColor& color)
    {
        int32 r = color.r * 256;
        int32 g = color.g * 256;
        int32 b = color.b * 256;

        mHTMLLinkString = AZStd::string::format("<qt><style>a { color: rgb(%i, %i, %i); } a:hover { color: rgb(40, 40, 40); }</style><a href='%s'>%s</a></qt>", r, g, b, text, text);
        return mHTMLLinkString.c_str();
    }


    void EMStudioManager::MakeTransparentButton(QPushButton* button, const char* iconFileName, const char* toolTipText, uint32 width, uint32 height)
    {
        button->setObjectName("TransparentButton");
        button->setToolTip(toolTipText);
        button->setMinimumSize(width, height);
        button->setMaximumSize(width, height);
        button->setIcon(MysticQt::GetMysticQt()->FindIcon(iconFileName));
    }


    void EMStudioManager::MakeTransparentMenuButton(QPushButton* button, const char* iconFileName, const char* toolTipText, uint32 width, uint32 height)
    {
        button->setToolTip(toolTipText);
        button->setMinimumSize(width, height);
        button->setMaximumSize(width, height);
        button->setIcon(MysticQt::GetMysticQt()->FindIcon(iconFileName));

        button->setObjectName("EMFXMenuButton");
        button->setStyleSheet("QPushButton#EMFXMenuButton::menu-indicator \
                               { \
                                   subcontrol-position: right bottom; \
                                   subcontrol-origin: padding; \
                                   left: 0px; \
                                   top: -2px; \
                               }");
    }


    QLabel* EMStudioManager::MakeSeperatorLabel(uint32 width, uint32 height)
    {
        QLabel* seperatorLabel = new QLabel("");
        seperatorLabel->setObjectName("SeperatorLabel");
        seperatorLabel->setMinimumSize(width, height);
        seperatorLabel->setMaximumSize(width, height);

        return seperatorLabel;
    }


    void EMStudioManager::SetVisibleJointIndices(const AZStd::unordered_set<AZ::u32>& visibleJointIndices)
    {
        m_visibleJointIndices = visibleJointIndices;
    }

    void EMStudioManager::SetSelectedJointIndices(const AZStd::unordered_set<AZ::u32>& selectedJointIndices)
    {
        m_selectedJointIndices = selectedJointIndices;
    }

    void EMStudioManager::JointSelectionChanged()
    {
        AZ::Outcome<const QModelIndexList&> selectedRowIndicesOutcome;
        EMotionFX::SkeletonOutlinerRequestBus::BroadcastResult(selectedRowIndicesOutcome, &EMotionFX::SkeletonOutlinerRequests::GetSelectedRowIndices);
        if (!selectedRowIndicesOutcome.IsSuccess())
        {
            return;
        }

        m_selectedJointIndices.clear();

        const QModelIndexList& selectedRowIndices = selectedRowIndicesOutcome.GetValue();
        if (selectedRowIndices.empty())
        {
            return;
        }

        for (const QModelIndex& selectedIndex : selectedRowIndices)
        {
            EMotionFX::Node* joint = selectedIndex.data(EMotionFX::SkeletonModel::ROLE_POINTER).value<EMotionFX::Node*>();
            m_selectedJointIndices.emplace(joint->GetNodeIndex());
        }
    }


    // before executing a command
    void EMStudioManager::EventProcessingCallback::OnPreExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine)
    {
        MCORE_UNUSED(group);
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        //  EMStudio::GetApp()->blockSignals(true);
        EMStudio::GetApp()->setOverrideCursor(QCursor(Qt::WaitCursor));
        //EMStudio::GetApp()->processEvents();
    }


    // after executing a command
    void EMStudioManager::EventProcessingCallback::OnPostExecuteCommand(MCore::CommandGroup* group, MCore::Command* command, const MCore::CommandLine& commandLine, bool wasSuccess, const AZStd::string& outResult)
    {
        MCORE_UNUSED(group);
        MCORE_UNUSED(command);
        MCORE_UNUSED(commandLine);
        MCORE_UNUSED(wasSuccess);
        MCORE_UNUSED(outResult);
        //EMStudio::GetApp()->processEvents();
        //  EMStudio::GetApp()->blockSignals(false);
        EMStudio::GetApp()->restoreOverrideCursor();
    }


    AZStd::string EMStudioManager::GetAppDataFolder() const
    {
        AZStd::string appDataFolder = QStandardPaths::standardLocations(QStandardPaths::DataLocation).at(0).toUtf8().data();
        appDataFolder += "/EMotionStudio/";

        QDir dir(appDataFolder.c_str());
        dir.mkpath(appDataFolder.c_str());

        EBUS_EVENT(AzFramework::ApplicationRequests::Bus, NormalizePathKeepCase, appDataFolder);
        return appDataFolder.c_str();
    }


    AZStd::string EMStudioManager::GetRecoverFolder() const
    {
        // Set the recover path
        const AZStd::string recoverPath = GetAppDataFolder() + "Recover" + AZ_CORRECT_FILESYSTEM_SEPARATOR;

        // create all folders needed
        QDir dir;
        dir.mkpath(recoverPath.c_str());

        // return the recover path
        return recoverPath;
    }


    AZStd::string EMStudioManager::GetAutosavesFolder() const
    {
        // Set the autosaves path
        const AZStd::string autosavesPath = GetAppDataFolder() + "Autosaves" + AZ_CORRECT_FILESYSTEM_SEPARATOR;

        // create all folders needed
        QDir dir;
        dir.mkpath(autosavesPath.c_str());

        // return the autosaves path
        return autosavesPath;
    }


    // function to add a gizmo to the manager
    MCommon::TransformationManipulator* EMStudioManager::AddTransformationManipulator(MCommon::TransformationManipulator* manipulator)
    {
        // check if manipulator exists
        if (manipulator == nullptr)
        {
            return nullptr;
        }

        // add and return the manipulator
        mTransformationManipulators.Add(manipulator);
        return manipulator;
    }


    // remove the given gizmo from the array
    void EMStudioManager::RemoveTransformationManipulator(MCommon::TransformationManipulator* manipulator)
    {
        mTransformationManipulators.RemoveByValue(manipulator);
    }


    // returns the gizmo array
    MCore::Array<MCommon::TransformationManipulator*>* EMStudioManager::GetTransformationManipulators()
    {
        return &mTransformationManipulators;
    }


    // new temporary helper function for text drawing
    void EMStudioManager::RenderText(QPainter& painter, const QString& text, const QColor& textColor, const QFont& font, const QFontMetrics& fontMetrics, Qt::Alignment textAlignment, const QRect& rect)
    {
        painter.setFont(font);
        painter.setPen(Qt::NoPen);
        painter.setBrush(textColor);

        const float textWidth       = fontMetrics.width(text);
        const float halfTextWidth   = textWidth * 0.5 + 0.5;
        const float halfTextHeight  = fontMetrics.height() * 0.5 + 0.5;
        const QPoint rectCenter     = rect.center();

        QPoint textPos;
        textPos.setY(rectCenter.y() + halfTextHeight - 1);

        switch (textAlignment)
        {
        case Qt::AlignLeft:
        {
            textPos.setX(rect.left() - 2);
            break;
        }

        case Qt::AlignRight:
        {
            textPos.setX(rect.right() - textWidth + 1);
            break;
        }

        default:
        {
            textPos.setX(rectCenter.x() - halfTextWidth + 1);
        }
        }

        QPainterPath path;
        path.addText(textPos, font, text);
        painter.drawPath(path);
    }

    //--------------------------------------------------------------------------
    // class Initializer
    //--------------------------------------------------------------------------
    // initialize EMotion Studio
    bool Initializer::Init(QApplication* app, int& argc, char* argv[])
    {
        // do nothing if we already have initialized
        if (gEMStudioMgr)
        {
            return true;
        }

        // create the new EMStudio object
        gEMStudioMgr = new EMStudioManager(app, argc, argv);

        // return success
        return true;
    }


    // the shutdown function
    void Initializer::Shutdown()
    {
        delete gEMStudioMgr;
        gEMStudioMgr = nullptr;
    }
} // namespace EMStudio
