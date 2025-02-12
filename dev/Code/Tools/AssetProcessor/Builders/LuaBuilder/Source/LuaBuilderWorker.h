/*
 * All or portions of this file Copyright(c) Amazon.com, Inc.or its affiliates or
 * its licensors.
 *
 * For complete copyright and license terms please see the LICENSE at the root of this
 * distribution (the "License"). All use of this software is governed by the License,
 *or, if provided, by the license below or the license accompanying this file. Do not
 * remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
 *WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *
 */
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Outcome/Outcome.h>
#include <AssetBuilderSDK/AssetBuilderBusses.h>
#include <AssetBuilderSDK/AssetBuilderSDK.h>

namespace LuaBuilder
{
    class LuaBuilderWorker
        : public AssetBuilderSDK::AssetBuilderCommandBus::Handler
    {
    public:
        AZ_TYPE_INFO(LuaBuilderWorker, "{166A7962-A3E4-4451-AC1A-AAD32E29C52C}");
        ~LuaBuilderWorker() override = default;

        //! Asset Builder Callback Functions
        void CreateJobs(const AssetBuilderSDK::CreateJobsRequest& request, AssetBuilderSDK::CreateJobsResponse& response);
        void ProcessJob(const AssetBuilderSDK::ProcessJobRequest& request, AssetBuilderSDK::ProcessJobResponse& response);

        //////////////////////////////////////////////////////////////////////////
        // AssetBuilderSDK::AssetBuilderCommandBus
        void ShutDown() override;
        //////////////////////////////////////////////////////////////////////////

    private:
        using JobStepOutcome = AZ::Outcome<AssetBuilderSDK::JobProduct, AssetBuilderSDK::ProcessJobResultCode>;

        JobStepOutcome RunCompileJob(const AssetBuilderSDK::ProcessJobRequest& request);
        JobStepOutcome RunCopyJob(const AssetBuilderSDK::ProcessJobRequest& request);

        bool m_isShuttingDown = false;
    };
}
