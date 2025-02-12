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


#include <AZCore/RTTI/RTTI.h>
#include <AzCore/Asset/AssetCommon.h>
#include <AZCore/Component/Component.h>
#include <AzCore/Math/Vector3.h>
#include <AzFramework/Entity/EntityDebugDisplayBus.h>
#include <AzCore/PlatformDef.h>
#include <Vegetation/Ebuses/InstanceSystemRequestBus.h>
#include <Vegetation/Ebuses/SystemConfigurationBus.h>
#include <Vegetation/Ebuses/DebugRequestsBus.h>
#include <Vegetation/Ebuses/DebugNotificationBus.h>
#include <Vegetation/Ebuses/DebugSystemDataBus.h>
#include <Vegetation/InstanceData.h>

namespace LmbrCentral
{
    template<typename, typename>
    class EditorWrappedComponentBase;
}

namespace Vegetation
{
    //////////////////////////////////////////////////////////////////////////

    class DebugConfig
        : public AZ::ComponentConfig
    {
    public:
        AZ_CLASS_ALLOCATOR(DebugConfig, AZ::SystemAllocator, 0);
        AZ_RTTI(DebugConfig, "{10750041-ABCA-4515-8D5D-B3E4769C3829}", AZ::ComponentConfig);
        static void Reflect(AZ::ReflectContext* context);

        DebugRequests::FilterTypeLevel m_filterLevel = DebugRequests::FilterTypeLevel::Warning;
        DebugRequests::SortType m_sortType = DebugRequests::SortType::BySector;
        AZ::u32 m_collectionFrequencyUs = 500000;
        AZ::u32 m_minThresholdUs = 500;
        AZ::u32 m_maxThresholdUs = 1500;
        AZ::u32 m_maxLabelDisplayDistance = 40;
        AZ::u32 m_maxDatapointDisplayCount = 1000;
        bool m_showVisualization = {};
        bool m_showDebugStats = {};
        bool m_showInstanceVisualization = {};
    };

    //////////////////////////////////////////////////////////////////////////

    class DebugComponent
        : public AZ::Component
        , private AzFramework::DebugDisplayEventBus::Handler
        , private DebugRequestBus::Handler
        , private DebugNotificationBus::Handler
        , private SystemConfigurationRequestBus::Handler
    {
    public:
        template<typename, typename> friend class LmbrCentral::EditorWrappedComponentBase;
        AZ_COMPONENT(DebugComponent, "{E62A9E15-E763-4069-8AE5-93276F1E7AC7}");
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& services);
        static void Reflect(AZ::ReflectContext* context);

        DebugComponent(const DebugConfig& configuration) : m_configuration(configuration) {}
        DebugComponent() = default;
        ~DebugComponent() = default;

        //////////////////////////////////////////////////////////////////////////
        // AZ::Component interface implementation
        void Activate() override;
        void Deactivate() override;
        bool ReadInConfig(const AZ::ComponentConfig* baseConfig) override;
        bool WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const override;

        //////////////////////////////////////////////////////////////////////////
        // EntityDebugDisplayEventBus
        void DrawGlobalDebugInfo() override;

        //////////////////////////////////////////////////////////////////////////
        // DebugNotifications
        void FillSectorStart(int sectorX, int sectorY, TimePoint timePoint) override;
        void FillSectorEnd(int sectorX, int sectorY, TimePoint timePoint, AZ::u32 unusedClaimPointCount) override;
        void FillAreaStart(AZ::EntityId areaId, TimePoint timePoint) override;
        void MarkAreaRejectedByMask(AZ::EntityId areaId) override;
        void FillAreaEnd(AZ::EntityId areaId, TimePoint timePoint, AZ::u32 unusedClaimPointCount) override;

        void SetAreaDebugColor(AZ::EntityId areaId, AZ::Color debugColor, bool render) override;

        void FilterInstance(AZ::EntityId areaId, AZStd::string_view filterReason) override;
        void CreateInstance(InstanceId instanceId, AZ::Vector3 position, AZ::EntityId areaId) override;
        void DeleteInstance(InstanceId instanceId) override;
        void DeleteAllInstances() override;

        void ExportCurrentReport() override;
        void ToggleVisualization() override;

        //////////////////////////////////////////////////////////////////////////
        // DebugRequests
        void GetPerformanceReport(PerformanceReport& report) const override;
        void DumpPerformanceReport(const PerformanceReport& report, FilterTypeLevel filter, SortType sort) const override;
        void ClearPerformanceReport() override;

        //////////////////////////////////////////////////////////////////////////
        // SystemConfigurationRequestBus
        void UpdateSystemConfig(const AZ::ComponentConfig* config) override;
        void GetSystemConfig(AZ::ComponentConfig* config) const override {}; // ignore this call


    protected:
        void PrepareNextReport();
        void DrawSectorTimingData();
        void CopyReportToSortedList();
        void AddConsoleVariables();
        void RemoveConsoleVariables();
        void DrawDebugStats();
        void DrawInstanceDebug();

    private:
        AZStd::atomic_bool m_exportCurrentReport{ false };

        mutable AZStd::recursive_mutex m_reportMutex;
        DebugRequests::PerformanceReport m_thePerformanceReport;

        DebugConfig m_configuration;
        TimePoint m_lastCollectionTime = {};

        // internal tracking
        struct SectorAreaData
        {
            TimePoint m_start;
            TimePoint m_end;
            size_t m_numInstancesCreated = 0; // number of instances in this sector/area combination
            FilterReasonCount m_numInstancesRejectedByFilters;
            bool m_filteredByMasks = false; // if this area was filtered because of the inclusive/exclusive masks in this sector/area combination
        };
        struct SectorTracker
        {
            SectorId m_id;
            TimePoint m_start;
            TimePoint m_end;
            size_t m_numInstancesCreated = 0;// number of instances in the sector over all areas.
            size_t m_numClaimPointsRemaining = 0;
            AZStd::unordered_map<AreaId, SectorAreaData> m_perAreaTracking;
        };
        using SectorData = AZStd::vector<SectorTracker>;
        SectorTracker m_currentSectorTiming;
        SectorData m_sectorData;

        struct AreaTracker
        {
            AreaId m_id;
            TimePoint m_start;
            TimePoint m_end;
            SectorId m_sectorId;
            size_t m_numInstancesCreated; // number of instances in this area over all sectors
            FilterReasonCount m_numInstancesRejectedByFilters;
            size_t m_numClaimPointsRemaining = 0;
            bool m_filteredByMasks; // true if this area was always filtered
        };

        using AreaData = AZStd::vector<AreaTracker>;
        AZStd::size_t MakeAreaSectorKey(AZ::EntityId areaId, SectorId sectorId);
        AZStd::unordered_map<uint64, AreaTracker> m_currentAreasTiming;
        AreaData m_areaData;

        AZStd::vector<SectorTiming> m_currentSortedTimingList;

        //! Cached pointer to the veg system debug data
        DebugData* m_debugData = nullptr;

        struct DebugInstanceData
        {
            AZ::Vector3 m_position;
            AreaId m_areaId;
        };
        AZStd::unordered_map<InstanceId, DebugInstanceData> m_activeInstances;

        struct DebugColorData
        {
            AZ::Color m_color = AZ::Color(1.0f, 0.0f, 1.0f, 1.0f);
            bool m_render = true;
        };
        AZStd::unordered_map<AreaId, DebugColorData> m_areaDebugColors;
    };

} // namespace Vegetation

