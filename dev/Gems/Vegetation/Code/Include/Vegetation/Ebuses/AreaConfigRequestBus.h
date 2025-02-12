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

#include <AzCore/Component/ComponentBus.h>

namespace Vegetation
{
    enum class AreaLayer : AZ::u32
    {
        Background = 0,
        Foreground,
    };

    class AreaConfigRequests
        : public AZ::ComponentBus
    {
    public:
        /**
         * Overrides the default AZ::EBusTraits handler policy to allow one
         * listener only.
         */
        static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;

        virtual float GetAreaPriority() const = 0;
        virtual void SetAreaPriority(float priority) = 0;

        virtual AreaLayer GetAreaLayer() const = 0;
        virtual void SetAreaLayer(AreaLayer type) = 0;

        virtual AZ::u32 GetAreaProductCount() const = 0;
    };
}