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


#include <AWSNativeSDKInit/AWSLogSystemInterface.h>

#include <AzCore/base.h>
#include <AzCore/Module/Environment.h>

#include <stdarg.h>

#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/DefaultLogSystem.h>
#include <aws/core/utils/logging/ConsoleLogSystem.h>
#endif

namespace AWSNativeSDKInit
{
    const char* AWSLogSystemInterface::AWS_API_LOG_PREFIX = "AwsApi-";
    const int AWSLogSystemInterface::MAX_MESSAGE_LENGTH = 4096;
    const char* AWSLogSystemInterface::MESSAGE_FORMAT = "[AWS] %s - %s";
    const char* AWSLogSystemInterface::ERROR_WINDOW_NAME = "AwsNativeSDK"; 

    AWSLogSystemInterface::AWSLogSystemInterface(Aws::Utils::Logging::LogLevel logLevel)
        : m_logLevel(logLevel)
    {
    }

    /**
    * Gets the currently configured log level for this logger.
    */
    Aws::Utils::Logging::LogLevel AWSLogSystemInterface::GetLogLevel() const
    {
        Aws::Utils::Logging::LogLevel newLevel = m_logLevel;
        static const char* const logLevelEnvVar = "sys_SetLogLevel";
        auto logVar = AZ::Environment::FindVariable<int>(logLevelEnvVar);

        if (logVar)
        {
            newLevel = (Aws::Utils::Logging::LogLevel) *logVar;
        }

        return newLevel != m_logLevel ? newLevel : m_logLevel;
    }

    /**
    * Does a printf style output to the output stream. Don't use this, it's unsafe. See LogStream
    */
    void AWSLogSystemInterface::Log(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* formatStr, ...)
    {

        if (!ShouldLog(logLevel))
        {
            return;
        }

        char message[MAX_MESSAGE_LENGTH];

        va_list mark;
        va_start(mark, formatStr);
        azvsnprintf(message, MAX_MESSAGE_LENGTH, formatStr, mark);
        va_end(mark);

        ForwardAwsApiLogMessage(logLevel, tag, message);

    }

    /**
    * Writes the stream to the output stream.
    */
    void AWSLogSystemInterface::LogStream(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const Aws::OStringStream &messageStream)
    {

        if(!ShouldLog(logLevel)) 
        {
            return;
        }

        ForwardAwsApiLogMessage(logLevel, tag, messageStream.str().c_str());

    }

    bool AWSLogSystemInterface::ShouldLog(Aws::Utils::Logging::LogLevel logLevel)
    {
#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
        Aws::Utils::Logging::LogLevel newLevel = GetLogLevel();

        if (newLevel > Aws::Utils::Logging::LogLevel::Info && newLevel <= Aws::Utils::Logging::LogLevel::Trace && newLevel != m_logLevel)
        {
            SetLogLevel(newLevel);
        }
#endif
        return (logLevel <= m_logLevel);
    }

    void AWSLogSystemInterface::SetLogLevel(Aws::Utils::Logging::LogLevel newLevel)
    {
#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
        Aws::Utils::Logging::ShutdownAWSLogging();
        Aws::Utils::Logging::InitializeAWSLogging(Aws::MakeShared<AWSLogSystemInterface>("AWS", newLevel));
        m_logLevel = newLevel;
#endif
    }

    void AWSLogSystemInterface::ForwardAwsApiLogMessage(Aws::Utils::Logging::LogLevel logLevel, const char* tag, const char* message)
    {
#if defined(PLATFORM_SUPPORTS_AWS_NATIVE_SDK)
        switch (logLevel)
        {

        case Aws::Utils::Logging::LogLevel::Off:
            break;

        case Aws::Utils::Logging::LogLevel::Fatal:
            AZ::Debug::Trace::Instance().Error(__FILE__, __LINE__, AZ_FUNCTION_SIGNATURE, AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        case Aws::Utils::Logging::LogLevel::Error:
            AZ::Debug::Trace::Instance().Warning(__FILE__, __LINE__, AZ_FUNCTION_SIGNATURE, AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        case Aws::Utils::Logging::LogLevel::Warn:
            AZ::Debug::Trace::Instance().Warning(__FILE__, __LINE__, AZ_FUNCTION_SIGNATURE, AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        case Aws::Utils::Logging::LogLevel::Info:
            AZ::Debug::Trace::Instance().Printf(AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        case Aws::Utils::Logging::LogLevel::Debug:
            AZ::Debug::Trace::Instance().Printf(AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        case Aws::Utils::Logging::LogLevel::Trace:
            AZ::Debug::Trace::Instance().Printf(AWSLogSystemInterface::ERROR_WINDOW_NAME, MESSAGE_FORMAT, tag, message);
            break;

        default:
            break;

        }
#endif
    }
}
