//===-- PlatformAndroid.h ---------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_PlatformAndroid_h_
#define liblldb_PlatformAndroid_h_

// C Includes
// C++ Includes

#include <string>

// Other libraries and framework includes
// Project includes
#include "Plugins/Platform/Linux/PlatformLinux.h"

namespace lldb_private {
namespace platform_android {

    class PlatformAndroid : public platform_linux::PlatformLinux
    {
    public:
        static void
        Initialize ();

        static void
        Terminate ();

        PlatformAndroid (bool is_host);

        virtual
        ~PlatformAndroid();

        //------------------------------------------------------------
        // lldb_private::PluginInterface functions
        //------------------------------------------------------------
        static lldb::PlatformSP
        CreateInstance (bool force, const ArchSpec *arch);

        static ConstString
        GetPluginNameStatic (bool is_host);

        static const char *
        GetPluginDescriptionStatic (bool is_host);

        ConstString
        GetPluginName() override;
        
        uint32_t
        GetPluginVersion() override
        {
            return 1;
        }

        //------------------------------------------------------------
        // lldb_private::Platform functions
        //------------------------------------------------------------

        Error
        ConnectRemote (Args& args) override;

    protected:
        const char *
        GetCacheHostname () override;

    private:
        std::string m_device_id;
        DISALLOW_COPY_AND_ASSIGN (PlatformAndroid);
    };

} // namespace platofor_android
} // namespace lldb_private

#endif  // liblldb_PlatformAndroid_h_
