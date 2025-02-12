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

#include <IGameFramework.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityBus.h>
#include <AzCore/RTTI/TypeInfo.h>

#include <LyShine/Bus/UiCanvasManagerBus.h>

#include <LyShine/Bus/UiCanvasBus.h>
#include <LyShine/Bus/UiInteractableBus.h>
#include <LyShine/Bus/UiAnimationBus.h>
#include <LyShine/UiEntityContext.h>
#include <LyShine/UiComponentTypes.h>

#include "UiElementComponent.h"
#include "UiSerialize.h"
#include "Animation/UiAnimationSystem.h"

#include <IFont.h>

namespace AZ
{
    class SerializeContext;
}

namespace AzFramework
{
    class InputChannel;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
class UiCanvasManager
    : protected UiCanvasManagerBus::Handler
    , protected UiCanvasOrderNotificationBus::Handler
    , protected UiCanvasEnabledStateNotificationBus::Handler
    , protected FontNotificationBus::Handler
    , private AzFramework::AssetCatalogEventBus::Handler
{
public: // member functions

    //! Constructor, constructed by the LyShine class
    UiCanvasManager();
    ~UiCanvasManager() override;

public: // member functions

    // UiCanvasManagerBus interface implementation
    AZ::EntityId CreateCanvas() override;
    AZ::EntityId LoadCanvas(const AZStd::string& canvasPathname) override;
    void UnloadCanvas(AZ::EntityId canvasEntityId) override;
    AZ::EntityId FindLoadedCanvasByPathName(const AZStd::string& canvasPathname) override;
    CanvasEntityList GetLoadedCanvases() override;
    void SetLocalUserIdInputFilterForAllCanvases(AzFramework::LocalUserId localUserId) override;
    // ~UiCanvasManagerBus

    // UiCanvasOrderNotificationBus
    void OnCanvasDrawOrderChanged(AZ::EntityId canvasEntityId) override;
    // ~UiCanvasOrderNotificationBus

    // UiCanvasEnabledStateNotificationBus
    void OnCanvasEnabledStateChanged(AZ::EntityId canvasEntityId, bool enabled) override;
    // ~UiCanvasEnabledStateNotificationBus

    // FontNotifications
    void OnFontsReloaded() override;
    void OnFontTextureUpdated(IFFont* font) override;
    // ~FontNotifications

    // AssetCatalogEventBus::Handler
    void OnCatalogAssetChanged(const AZ::Data::AssetId& assetId) override;
    // ~AssetCatalogEventBus::Handler

    AZ::EntityId CreateCanvasInEditor(UiEntityContext* entityContext);
    AZ::EntityId LoadCanvasInEditor(const string& assetIdPathname, const string& sourceAssetPathname, UiEntityContext* entityContext);
    AZ::EntityId ReloadCanvasFromXml(const AZStd::string& xmlString, UiEntityContext* entityContext);

    void ReleaseCanvas(AZ::EntityId canvas, bool forEditor);

    // Wait until canvas processing is completed before deleting the UI canvas to prevent deleting a UI canvas
    // from an active entity within that UI canvas, such as unloading a UI canvas from a script canvas that is
    // on an element in that UI canvas (Used when UI canvas is loaded in game)
    void ReleaseCanvasDeferred(AZ::EntityId canvas);

    AZ::EntityId FindCanvasById(LyShine::CanvasId id);

    void SetTargetSizeForLoadedCanvases(AZ::Vector2 viewportSize);
    void UpdateLoadedCanvases(float deltaTimeInSeconds);
    void RenderLoadedCanvases();

    void DestroyLoadedCanvases(bool keepCrossLevelCanvases);

    void OnLoadScreenUnloaded();

    // These functions handle events for all canvases loaded in the game
    bool HandleInputEventForLoadedCanvases(const AzFramework::InputChannel& inputChannel);
    bool HandleTextEventForLoadedCanvases(const AZStd::string& textUTF8);

#ifndef _RELEASE
    void DebugDisplayCanvasData(int setting) const;
    void DebugDisplayDrawCallData() const;
    void DebugReportDrawCalls(const AZStd::string& name) const;
    void DebugDisplayElemBounds(int canvasIndexFilter) const;
#endif

private: // member functions

    AZ_DISABLE_COPY_MOVE(UiCanvasManager);

    void SortCanvasesByDrawOrder();

    UiCanvasComponent* FindCanvasComponentByPathname(const string& name);
    UiCanvasComponent* FindEditorCanvasComponentByPathname(const string& name);

    // Handle input event for all loaded canvases
    bool HandleInputEventForLoadedCanvases(const AzFramework::InputChannel::Snapshot& inputSnapshot,
        const AZ::Vector2& viewportPos,
        AzFramework::ModifierKeyMask activeModifierKeys,
        bool isPositional);

    // Handle input event for all in world canvases (canvases that render to a texture)
    bool HandleInputEventForInWorldCanvases(const AzFramework::InputChannel::Snapshot& inputSnapshot, const AZ::Vector2& viewportPos);
    
    // Generate and handle a mouse position input event for all loaded canvases
    void GenerateMousePositionInputEvent();

    AZ::EntityId LoadCanvasInternal(const string& assetIdPathname, bool forEditor, const string& sourceAssetPathname, UiEntityContext* entityContext,
        const AZ::SliceComponent::EntityIdToEntityIdMap* previousRemapTable = nullptr, AZ::EntityId previousCanvasId = AZ::EntityId());

    void QueueCanvasForDeletion(AZ::EntityId canvasEntityId);
    void DeleteCanvasesQueuedForDeletion();

#ifndef _RELEASE
    AZStd::string DebugGetElementName(AZ::EntityId entityId, int maxLength) const;
#endif

private: // types

    typedef std::vector<UiCanvasComponent*> CanvasList;   //!< Sorted by draw order

private: // data

    CanvasList m_loadedCanvases;             // UI Canvases loaded in game
    CanvasList m_loadedCanvasesInEditor;     // UI Canvases loaded in editor

    AZ::Vector2 m_latestViewportSize;        // The most recent viewport size

    int m_recursionGuardCount = 0;   // incremented while updating or doing input handling for canvases
    AZStd::vector<AZ::EntityId> m_canvasesQueuedForDeletion;

    bool m_fontTextureHasChanged = false;

    AzFramework::LocalUserId m_localUserIdInputFilter; // The local user id to filter UI input on

    // Indicates whether to generate a mouse position input event on the next canvas update.
    // Used to update the canvas' interactable hover states even when the mouse position hasn't changed
    bool m_generateMousePositionInputEvent = false;
};
