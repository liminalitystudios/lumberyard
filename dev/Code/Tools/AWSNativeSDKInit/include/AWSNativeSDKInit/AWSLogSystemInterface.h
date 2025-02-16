/*
* All or portions of this file Copyright(c) Amazon.com, Inc.or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
#include <aws/core/utils/logging/LogSystemInterface.h>
#else
#include <sstream>
namespace Aws
{
    using OStringStream = std::basic_ostringstream<char>;
    namespace Utils
    {
        namespace Logging
        {
            using LogLevel = int;
        }
    }
}
#endif
namespace AWSNativeSDKInit
{
    class AWSLogSystemInterface 
#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
        : public Aws::Utils::Logging::LogSystemInterface
#endif
    {

    public:
        static const char* AWS_API_LOG_PREFIX;
        static const int MAX_MESSAGE_LENGTH;
        static const char* MESSAGE_FORMAT;
        static const char* ERROR_WINDOW_NAME;
        static const char* LOG_ENV_VAR;

        AWSLogSystemInterface(Aws::Utils::Logging::LogLevel logLevel);

        /**
        * Gets the currently configured log level for this logger.
        */
#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
        Aws::Utils::Logging::LogLevel GetLogLevel(void) const override;
#else
        Aws::Utils::Logging::LogLevel GetLogLevel(void) const;
#endif
        /**
        * Does a printf style output to the output stream. Don't use this, it's unsafe. See LogStream
        */
        void Log(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* formatStr, ...);

        /**
        * Writes the stream to the output stream.
        */
        void LogStream(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const Aws::OStringStream &messageStream);

    private:
        bool ShouldLog(Aws::Utils::Logging::LogLevel logLevel);
        void SetLogLevel(Aws::Utils::Logging::LogLevel newLevel);
        void ForwardAwsApiLogMessage(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* message);

        Aws::Utils::Logging::LogLevel m_logLevel;
    };

}
