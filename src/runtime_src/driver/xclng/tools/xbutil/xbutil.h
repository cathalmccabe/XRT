/**
 * Copyright (C) 2016-2018 Xilinx, Inc
 * Author: Sonal Santan, Ryan Radjabi
 * Simple command line utility to inetract with SDX PCIe devices
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */
#ifndef XBUTIL_H
#define XBUTIL_H

#include <fstream>
#include <assert.h>
#include <vector>
#include <map>
#include <sstream>
#include <string>

#include "driver/include/xclhal2.h"
#include "driver/include/xcl_axi_checker_codes.h"
#include "../user_common/dmatest.h"
#include "../user_common/memaccess.h"
#include "../user_common/dd.h"
#include "../user_common/utils.h"
#include "../user_common/sensor.h"
#include "scan.h"
#include "driver/include/xclbin.h"
#include <version.h>

#include <chrono>
using Clock = std::chrono::high_resolution_clock;

#define TO_STRING(x) #x
#define AXI_FIREWALL

#define XCL_NO_SENSOR_DEV_LL    ~(0ULL)
#define XCL_NO_SENSOR_DEV       ~(0UL)
#define XCL_NO_SENSOR_DEV_S     0xffff
#define XCL_INVALID_SENSOR_VAL 0

/*
 * Simple command line tool to query and interact with SDx PCIe devices
 * The tool statically links with xcldma HAL driver inorder to avoid
 * dependencies on environment variables like XILINX_OPENCL, LD_LIBRARY_PATH, etc.
 * TODO:
 * Rewrite the command line parsing to provide interface like Android adb:
 * xcldev <cmd> [options]
 */

namespace xcldev {

enum command {
    FLASH,
    PROGRAM,
    CLOCK,
    BOOT,
    HELP,
    QUERY,
    DUMP,
    RESET,
    RUN,
    FAN,
    DMATEST,
    LIST,
    SCAN,
    MEM,
    DD,
    STATUS,
    CMD_MAX,
    TOP
};
enum subcommand {
    MEM_READ = 0,
    MEM_WRITE,
    STATUS_SPM,
    STATUS_LAPC,
    STATUS_SSPM,
    STREAM,
    STATUS_UNSUPPORTED,
    MEM_QUERY_ECC,
    MEM_RESET_ECC
};
enum statusmask {
    STATUS_NONE_MASK = 0x0,
    STATUS_SPM_MASK = 0x1,
    STATUS_LAPC_MASK = 0x2,
    STATUS_SSPM_MASK = 0x4
};

static const std::pair<std::string, command> map_pairs[] = {
    std::make_pair("flash", FLASH),
    std::make_pair("program", PROGRAM),
    std::make_pair("clock", CLOCK),
    std::make_pair("boot", BOOT),
    std::make_pair("help", HELP),
    std::make_pair("query", QUERY),
    std::make_pair("dump", DUMP),
    std::make_pair("reset", RESET),
    std::make_pair("run", RUN),
    std::make_pair("fan", FAN),
    std::make_pair("dmatest", DMATEST),
    std::make_pair("list", LIST),
    std::make_pair("scan", SCAN),
    std::make_pair("mem", MEM),
    std::make_pair("dd", DD),
    std::make_pair("status", STATUS),
    std::make_pair("top", TOP)

};

static const std::pair<std::string, subcommand> subcmd_pairs[] = {
    std::make_pair("read", MEM_READ),
    std::make_pair("write", MEM_WRITE),
    std::make_pair("spm", STATUS_SPM),
    std::make_pair("lapc", STATUS_LAPC),
    std::make_pair("sspm", STATUS_SSPM),
    std::make_pair("stream", STREAM),
    std::make_pair("query-ecc", MEM_QUERY_ECC),
    std::make_pair("reset-ecc", MEM_RESET_ECC)
};

static const std::vector<std::pair<std::string, std::string>> flash_types = {
    // bpi types
    std::make_pair( "7v3", "bpi" ),
    std::make_pair( "8k5", "bpi" ),
    std::make_pair( "ku3", "bpi" ),
    // spi types
    std::make_pair( "vu9p",    "spi" ),
    std::make_pair( "kcu1500", "spi" ),
    std::make_pair( "vcu1525", "spi" ),
    std::make_pair( "ku115",   "spi" )
};

static const std::map<std::string, command> commandTable(map_pairs, map_pairs + sizeof(map_pairs) / sizeof(map_pairs[0]));

class device {
    unsigned int m_idx;
    xclDeviceHandle m_handle;
    xclDeviceInfo2 m_devinfo;
    xclErrorStatus m_errinfo;

public:
    int domain() { return pcidev::get_dev(m_idx)->mgmt->domain; }
    int bus() { return pcidev::get_dev(m_idx)->mgmt->bus; }
    int dev() { return pcidev::get_dev(m_idx)->mgmt->dev; }
    int userFunc() { return pcidev::get_dev(m_idx)->user->func; }
    int mgmtFunc() { return pcidev::get_dev(m_idx)->mgmt->func; }
    device(unsigned int idx, const char* log) : m_idx(idx), m_handle(nullptr), m_devinfo{} {
        std::string devstr = "device[" + std::to_string(m_idx) + "]";
        m_handle = xclOpen(m_idx, log, XCL_QUIET);
        if (!m_handle)
            throw std::runtime_error("Failed to open " + devstr);
        if (xclGetDeviceInfo2(m_handle, &m_devinfo))
            throw std::runtime_error("Unable to obtain info from " + devstr);
#ifdef AXI_FIREWALL
        if (xclGetErrorStatus(m_handle, &m_errinfo))
            throw std::runtime_error("Unable to obtain AXI error from " + devstr);
#endif
    }

    device(device&& rhs) : m_idx(rhs.m_idx), m_handle(rhs.m_handle), m_devinfo(std::move(rhs.m_devinfo)) {
    }

    device(const device &dev) = delete;
    device& operator=(const device &dev) = delete;

    ~device() {
        xclClose(m_handle);
    }

    const char *name() const {
        return m_devinfo.mName;
    }

    /*
     * flash
     *
     * Determine flash method as BPI or SPI from flash_types table by the DSA name.
     * Override this if a flash type is passed in by command line switch.
     */
    int flash( const std::string& mcs1, const std::string& mcs2, std::string flashType )
    {
        std::cout << "Flash disabled. See 'xbflash'.\n";
        return 0;
    }

    int reclock2(unsigned regionIndex, const unsigned short *freq) {
        const unsigned short targetFreqMHz[4] = {freq[0], freq[1], 0, 0};
        return xclReClock2(m_handle, 0, targetFreqMHz);
    }

    int getComputeUnits(std::vector<ip_data> &computeUnits) const
    {
        std::string errmsg;
        std::vector<char> buf;
        pcidev::get_dev(m_idx)->user->sysfs_get("", "ip_layout", errmsg, buf);
        if (!errmsg.empty()) {
            std::cout << errmsg << std::endl;
            return -EINVAL;
        }
        if (buf.empty())
            return 0;

        const ip_layout *map = (ip_layout *)buf.data();
        if(map->m_count < 0)
            return -EINVAL;

        for(int i = 0; i < map->m_count; i++)
            computeUnits.emplace_back(map->m_ip_data[i]);
        return 0;
    }

    int parseComputeUnits(const std::vector<ip_data> &computeUnits) const
    {
        for( unsigned int i = 0; i < computeUnits.size(); i++ ) {
            static int cuIndex = 0;
            boost::property_tree::ptree ptCu;
            unsigned statusBuf;
            xclRead(m_handle, XCL_ADDR_KERNEL_CTRL, computeUnits.at( i ).m_base_address, &statusBuf, 4);
            ptCu.put( "count",        cuIndex );
            ptCu.put( "name",         computeUnits.at( i ).m_name );
            ptCu.put( "base_address", computeUnits.at( i ).m_base_address );
            ptCu.put( "status",       parseCUStatus( statusBuf ) );
            sensor_tree::add_child( "board.compute_unit.cu", ptCu );
            ++cuIndex;
        }
        return 0;
    }

    void m_devinfo_stringize_statics(const xclDeviceInfo2& m_devinfo,
        std::vector<std::string> &lines) const
    {
        std::stringstream ss, subss, ssdevice;
        std::string idcode, fpga, errmsg;
        pcidev::get_dev(m_idx)->mgmt->sysfs_get("icap", "idcode", errmsg, idcode);
        pcidev::get_dev(m_idx)->mgmt->sysfs_get("rom", "FPGA", errmsg, fpga);

        ss << std::left;
        ss << std::setw(16) << "DSA name" <<"\n";
        ss << std::setw(16) << sensor_tree::get( "board.dsa_name" ) << "\n\n";
        ss << std::setw(8) << m_devinfo.mName;
        ss << " [" << fpga << '(' << idcode << ")]\n\n";
        ss << std::setw(16) << "Vendor" << std::setw(16) << "Device";
        ss << std::setw(16) << "SubDevice" <<  std::setw(16) << "SubVendor";
        ss << std::setw(16) << "XMC fw version" << "\n";

        ss << std::setw(16) << std::hex << sensor_tree::get( "board.vendor" ) << std::dec;
        ss << std::setw(16) << std::hex << sensor_tree::get( "board.device" ) << std::dec;

        ssdevice << std::setw(4) << std::setfill('0') << std::hex << m_devinfo.mSubsystemId;
        ss << std::setw(16) << ssdevice.str();
        ss << std::setw(16) << std::hex << sensor_tree::get( "board.subdevice" ) << std::dec;

        // ptree needs help here
        ss << std::setw(16) << (m_devinfo.mXMCVersion != XCL_NO_SENSOR_DEV_LL ? m_devinfo.mXMCVersion : m_devinfo.mMBVersion) << "\n\n";

        // ptree needs help PB instead of GB
        ss << std::setw(16) << "DDR size" << std::setw(16) << "DDR count";
        ss << std::setw(16) << "Kernel Freq";

        subss << std::left << std::setw(16) << unitConvert( std::stoi( sensor_tree::get( "board.ddr_size" ) ) );
        subss << std::setw(16) << sensor_tree::get( "board.ddr_count" ) << std::setw(16) << " ";

        // ptree needs help here
        for(unsigned i= 0; i < m_devinfo.mNumClocks; ++i) {
            ss << "Clock" << std::setw(11) << i ;
            subss << m_devinfo.mOCLFrequency[i] << std::setw(13) << " MHz";
        }
        ss << "\n" << subss.str() << "\n\n";

        ss << std::setw(16) << "PCIe" << std::setw(32) << "DMA chan(bidir)";
        ss << std::setw(16) << "MIG Calibrated " << "\n";

        ss << "GEN " << sensor_tree::get( "board.pcie_speed" ) << "x" << std::setw(10) << sensor_tree::get( "board.pcie_width" );
        ss << std::setw(32) << sensor_tree::get( "board.dma_threads" );
        ss << std::setw(16) << std::boolalpha << sensor_tree::get( "board.mig_calibrated", "false" ) << std::noboolalpha << "\n";
        ss << std::right << std::setw(80) << std::setfill('#') << std::left << "\n";
        lines.push_back(ss.str());
   }

    void m_devinfo_stringize_power(const xclDeviceInfo2& m_devinfo,
        std::vector<std::string> &lines) const
    {
        std::stringstream ss;
        unsigned long long power;
        ss << std::left << "\n";

        ss << std::setw(16) << "Power" << "\n";
        power = m_devinfo.mPexCurr * m_devinfo.m12VPex +
            m_devinfo.mAuxCurr * m_devinfo.m12VAux;
        if(m_devinfo.mPexCurr != XCL_INVALID_SENSOR_VAL &&
            m_devinfo.mPexCurr != XCL_NO_SENSOR_DEV_LL &&
            m_devinfo.m12VPex != XCL_INVALID_SENSOR_VAL &&
            m_devinfo.m12VPex != XCL_NO_SENSOR_DEV_S){
            ss << std::setw(16)
                << std::to_string((float)power / 1000000).substr(0, 4) + "W"
                << "\n";
        } else {
            ss << std::setw(16) << "Not support" << "\n";
        }

        lines.push_back(ss.str());
    }

    void m_devinfo_stringize_dynamics(const xclDeviceInfo2& m_devinfo,
        std::vector<std::string> &lines) const
    {
        std::stringstream ss, subss;
        subss << std::left;
        std::string errmsg;
        std::string dna_info;

        ss << std::left << "\n";
        ss << std::setw(16) << "PCB TOP FRONT" << std::setw(16) << "PCB TOP REAR" << std::setw(16) << "PCB BTM FRONT" << "\n";
        unsigned short val = std::stoi(sensor_tree::get( "power.pcb_top_front" ) );
        if( ( val ==  (XCL_NO_SENSOR_DEV & (0xffff)) ) || ( val == XCL_INVALID_SENSOR_VAL ) )
            subss << std::setw(16) << "Not support";
        else
            subss << std::setw(16) << std::to_string( val )+" C";

        val = std::stoi( sensor_tree::get( "power.pcb_top_rear" ) );
        if( ( val ==  (XCL_NO_SENSOR_DEV & (0xffff)) ) || ( val == XCL_INVALID_SENSOR_VAL ) )
            subss << std::setw(16) << "Not support";
        else
            subss << std::setw(16) << std::to_string( val )+" C";

        val = std::stoi( sensor_tree::get( "power.pcb_btm_front" ) );
        if( ( val ==  (XCL_NO_SENSOR_DEV & (0xffff)) ) || ( val == XCL_INVALID_SENSOR_VAL ) )
            subss << std::setw(16) << "Not support";
        else
            subss << std::setw(16) << std::to_string( val )+" C";


        ss << "\n" << subss.str() << "\n\n";

        ss << std::setw(16) << "FPGA Temp" << std::setw(16) << "TCRIT Temp" << std::setw(16) << "Fan Speed" << "\n";
        ss << std::setw(16) << std::to_string(m_devinfo.mOnChipTemp) +" C";

        if((unsigned short)m_devinfo.mFanTemp == (XCL_NO_SENSOR_DEV & (0xffff)))
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.mFanTemp == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string(m_devinfo.mFanTemp) +" C";

        if(m_devinfo.mFanRpm == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support" << "\n\n";
        else if (m_devinfo.mFanRpm == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support" << "\n\n";
        else
            ss << std::setw(16) << std::to_string(m_devinfo.mFanRpm) +" rpm" << "\n\n";

        ss << std::setw(16) << "12V PEX" << std::setw(16) << "12V AUX";
        ss << std::setw(16) << "12V PEX Current" << std::setw(16) << "12V AUX Current" << "\n";

        if(m_devinfo.m12VPex == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.m12VPex == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else{
            float vol = (float)m_devinfo.m12VPex/1000;
            ss << std::setw(16) << std::to_string(vol).substr(0,4) + "V";
        }

        if(m_devinfo.m12VAux == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.m12VAux == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else{
            float vol = (float)m_devinfo.m12VAux/1000;
            ss << std::setw(16) << std::to_string(vol).substr(0,4) + "V";
        }

        if(m_devinfo.mPexCurr == XCL_NO_SENSOR_DEV)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.mPexCurr == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string(m_devinfo.mPexCurr) + "mA";


        if(m_devinfo.mAuxCurr == XCL_NO_SENSOR_DEV)
            ss << std::setw(16) << "Not support" << "\n\n";
        else if (m_devinfo.mAuxCurr == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support" << "\n\n";
        else
            ss << std::setw(16) << std::to_string(m_devinfo.mAuxCurr) + "mA" << "\n\n";


        ss << std::setw(16) << "3V3 PEX" << std::setw(16) << "3V3 AUX";
        ss << std::setw(16) << "DDR VPP BOTTOM" << std::setw(16) << "DDR VPP TOP" << "\n";

        if(m_devinfo.m3v3Pex == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.m3v3Pex == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m3v3Pex/1000).substr(0,4) + "V";


        if(m_devinfo.m3v3Aux == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.m3v3Aux == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m3v3Aux/1000).substr(0,4) + "V";


        if(m_devinfo.mDDRVppBottom == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.mDDRVppBottom == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mDDRVppBottom/1000).substr(0,4) + "V";


        if(m_devinfo.mDDRVppTop == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support" << "\n\n";
        else if (m_devinfo.mDDRVppTop == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support" << "\n\n";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mDDRVppTop/1000).substr(0,4) + "V" << "\n\n";


        ss << std::setw(16) << "SYS 5V5" << std::setw(16) << "1V2 TOP";
        ss << std::setw(16) << "1V8 TOP" << std::setw(16) << "0V85" << "\n";


        if(m_devinfo.mSys5v5 == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.mSys5v5 == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mSys5v5/1000).substr(0,4) + "V";


        if(m_devinfo.m1v2Top == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if (m_devinfo.m1v2Top == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m1v2Top/1000).substr(0,4) + "V";


        if(m_devinfo.m1v8Top == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.m1v8Top == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m1v8Top/1000).substr(0,4) + "V";



        if(m_devinfo.m0v85 == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support" << "\n\n";
        else if(m_devinfo.m0v85 == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support" << "\n\n";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m0v85/1000).substr(0,4) + "V" << "\n\n";


        ss << std::setw(16) << "MGT 0V9" << std::setw(16) << "12V SW";
        ss << std::setw(16) << "MGT VTT" << "\n";


        if(m_devinfo.mMgt0v9 == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.mMgt0v9 == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mMgt0v9/1000).substr(0,4) + "V";


        if(m_devinfo.m12vSW == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.m12vSW == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.m12vSW/1000).substr(0,4) + "V";


        if(m_devinfo.mMgtVtt == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support" << "\n\n";
        else if(m_devinfo.mMgtVtt == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support" << "\n\n";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mMgtVtt/1000).substr(0,4) + "V" << "\n\n";


        ss << std::setw(16) << "VCCINT VOL" << std::setw(16) << "VCCINT CURR" << std::setw(32) << "DNA" <<"\n";

        if(m_devinfo.mVccIntVol == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.mVccIntVol == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else
            ss << std::setw(16) << std::to_string((float)m_devinfo.mVccIntVol/1000).substr(0,4) + "V";


        if(m_devinfo.mVccIntCurr == XCL_NO_SENSOR_DEV_S)
            ss << std::setw(16) << "Not support";
        else if(m_devinfo.mVccIntCurr == XCL_INVALID_SENSOR_VAL)
            ss << std::setw(16) << "Not support";
        else{
            ss << std::setw(16) << (m_devinfo.mVccIntCurr >= 10000 ? (std::to_string(m_devinfo.mVccIntCurr) + "mA") : "<10A");
        }

        auto dev = pcidev::get_dev(m_idx);

        dev->mgmt->sysfs_get("dna", "dna", errmsg, dna_info);

        if(dna_info.empty())
            ss << std::setw(32) << "Not support" << "\n";
        else{
            ss << std::setw(32) << dna_info << "\n";
        }

        m_devinfo_stringize_power(m_devinfo, lines);

        ss << std::right << std::setw(80) << std::setfill('#') << std::left << "\n";
        lines.push_back(ss.str());
    }

    void m_devinfo_stringize(const xclDeviceInfo2& m_devinfo,
        std::vector<std::string> &lines) const
    {
        m_devinfo_stringize_statics(m_devinfo, lines);
        m_devinfo_stringize_dynamics(m_devinfo, lines);
    }

    void m_mem_usage_bar(xclDeviceUsage &devstat,
        std::vector<std::string> &lines) const
    {
        std::stringstream ss;
        std::string errmsg;
        std::vector<char> buf;

        ss << "Device Memory Usage\n";

        pcidev::get_dev(m_idx)->user->sysfs_get(
            "", "mem_topology", errmsg, buf);

        if (!errmsg.empty()) {
            ss << errmsg << std::endl;
            lines.push_back(ss.str());
            return;
        }

        const mem_topology *map = (mem_topology *)buf.data();

        if(buf.empty() || map->m_count < 0) {
            ss << "WARNING: 'mem_topology' invalid, unable to report topology. "
                << "Has the bitstream been loaded? See 'xbutil program'.";
            lines.push_back(ss.str());
            return;
        }

        if(map->m_count == 0) {
            ss << "-- none found --. See 'xbutil program'.";
            lines.push_back(ss.str());
            return;
        }

        unsigned numDDR = map->m_count;
        for(unsigned i = 0; i < numDDR; i++ ) {
            if(map->m_mem_data[i].m_type == MEM_STREAMING)
                continue;

            float percentage = (float)devstat.ddrMemUsed[i] * 100 /
                (map->m_mem_data[i].m_size << 10);
            int nums_fiftieth = (int)percentage / 2;
            std::string str = std::to_string(percentage).substr(0, 4) + "%";

            ss << " [" << i << "] "
                << std::setw(16 - (std::to_string(i).length()) - 4)
                << std::left << map->m_mem_data[i].m_tag;
            ss << "[ " << std::right << std::setw(nums_fiftieth)
                << std::setfill('|') << (nums_fiftieth ? " ":"")
                <<  std::setw(56 - nums_fiftieth);
            ss << std::setfill(' ') << str << " ]" << "\n";
        }

        lines.push_back(ss.str());
    }

    void getMemTopology( void ) const
    {
        std::string errmsg;
        std::vector<char> buf;
        pcidev::get_dev(m_idx)->user->sysfs_get("", "mem_topology", errmsg, buf);
        const mem_topology *map = (mem_topology *)buf.data();
        if(buf.empty())
            return;

        for(unsigned i = 0; i < (unsigned)map->m_count; i++) {
            boost::property_tree::ptree ptMem;
            ptMem.put( "index", i );
            ptMem.put( "type",  map->m_mem_data[i].m_type );
            ptMem.put( "tag",   map->m_mem_data[i].m_tag );
            ptMem.put( "used",  map->m_mem_data[i].m_used );
            ptMem.put( "size",  unitConvert(map->m_mem_data[i].m_size << 10) );
            sensor_tree::add_child( "board.memory.mem", ptMem );
        }
    }

    void m_mem_usage_stringize_dynamics(xclDeviceUsage &devstat,
        const xclDeviceInfo2& m_devinfo, std::vector<std::string> &lines) const
    {
        std::stringstream ss;
        std::string errmsg;
        std::vector<char> buf;

        ss << std::left << std::setw(48) << "Mem Topology"
            << std::setw(32) << "Device Memory Usage" << "\n";

        pcidev::get_dev(m_idx)->user->sysfs_get(
            "", "mem_topology", errmsg, buf);

        if (!errmsg.empty()) {
            ss << errmsg << std::endl;
            lines.push_back(ss.str());
            return;
        }

        const mem_topology *map = (mem_topology *)buf.data();
        unsigned numDDR = 0;

        if(!buf.empty())
            numDDR = map->m_count;

        if(numDDR == 0) {
            ss << "-- none found --. See 'xbutil program'.\n";
        } else if(numDDR < 0) {
            ss << "WARNING: 'mem_topology' invalid, unable to report topology. "
                << "Has the bitstream been loaded? See 'xbutil program'.";
            lines.push_back(ss.str());
            return;
        } else {
            ss << std::setw(16) << "Tag"  << std::setw(12) << "Type"
                << std::setw(12) << "Temp" << std::setw(8) << "Size";
            ss << std::setw(16) << "Mem Usage" << std::setw(8) << "BO nums"
                << "\n";
        }

        for(unsigned i = 0; i < numDDR; i++) {
            if (map->m_mem_data[i].m_type == MEM_STREAMING)
                continue;

            ss << " [" << i << "] " <<
                std::setw(16 - (std::to_string(i).length()) - 4) << std::left
                << map->m_mem_data[i].m_tag;

            std::string str;
            if(map->m_mem_data[i].m_used == 0) {
                str = "**UNUSED**";
            } else {
                std::map<MEM_TYPE, std::string> my_map = {
                    {MEM_DDR3, "MEM_DDR3"}, {MEM_DDR4, "MEM_DDR4"},
                    {MEM_DRAM, "MEM_DRAM"}, {MEM_STREAMING, "MEM_STREAMING"},
                    {MEM_PREALLOCATED_GLOB, "MEM_PREALLOCATED_GLOB"},
                    {MEM_ARE, "MEM_ARE"}, {MEM_HBM, "MEM_HBM"},
                    {MEM_BRAM, "MEM_BRAM"}, {MEM_URAM, "MEM_URAM"}
                };
                auto search = my_map.find((MEM_TYPE)map->m_mem_data[i].m_type );
                str = search->second;
            }

            ss << std::left << std::setw(12) << str;
            if (i < sizeof (m_devinfo.mDimmTemp) / sizeof (m_devinfo.mDimmTemp[0]) &&
                m_devinfo.mDimmTemp[i] != XCL_INVALID_SENSOR_VAL &&
                m_devinfo.mDimmTemp[i] != XCL_NO_SENSOR_DEV_S) {
                ss << std::setw(12) << std::to_string(m_devinfo.mDimmTemp[i]) + " C";
            } else {
                ss << std::setw(12) << "Not Supp";
            }

            ss << std::setw(8) << unitConvert(map->m_mem_data[i].m_size << 10);
            ss << std::setw(16) << unitConvert(devstat.ddrMemUsed[i]);
            // print size
            ss << std::setw(8) << std::dec << devstat.ddrBOAllocated[i] << "\n";
        }

        ss << "\nTotal DMA Transfer Metrics:" << "\n";
        for (unsigned i = 0; i < 2; i++) {
            ss << "  Chan[" << i << "].h2c:  " << unitConvert(devstat.h2c[i]) << "\n";
            ss << "  Chan[" << i << "].c2h:  " << unitConvert(devstat.c2h[i]) << "\n";
        }

#if 0 // Enable when all platforms with ERT are packaged with new firmware
        buf.clear();
        pcidev::get_dev(m_idx)->user->sysfs_get(
            "mb_scheduler", "kds_custat", errmsg, buf);

        if (buf.size()) {
          ss << "\nCompute Unit Usage:" << "\n";
          ss << buf.data() << "\n";
        }
#endif

        ss << std::setw(80) << std::setfill('#') << std::left << "\n";
        lines.push_back(ss.str());
    }

    /*
     * rewrite this function to place stream info in tree, dump will format the info.
     */
    void m_stream_usage_stringize_dynamics( const xclDeviceInfo2& m_devinfo,
        std::vector<std::string> &lines) const
    {
        std::stringstream ss;
        std::string errmsg;
        std::vector<char> buf;
        std::vector<std::string> attrs;

        ss << std::setfill(' ') << "\n";

        ss << std::left << std::setw(48) << "Stream Topology" << "\n";

        pcidev::get_dev(m_idx)->user->sysfs_get("", "mem_topology", errmsg, buf);

        if (!errmsg.empty()) {
            ss << errmsg << std::endl;
            lines.push_back(ss.str());
            return;
        }

        const mem_topology *map = (mem_topology *)buf.data();
        unsigned num = 0;

        if(!buf.empty())
            num = map->m_count;

        if(num == 0) {
            ss << "-- none found --. See 'xbutil program'.\n";
        } else if(num < 0) {
            ss << "WARNING: 'mem_topology' invalid, unable to report topology. "
               << "Has the bitstream been loaded? See 'xbutil program'.";
            lines.push_back(ss.str());
            return;
        } else {
            ss << std::setw(16) << "Tag"  << std::setw(8) << "Route"
               << std::setw(5) << "Flow" << std::setw(10) << "Status"
               << std::setw(14) << "Request(B/#)" << std::setw(14) << "Complete(B/#)"
               << std::setw(10) << "Pending(B/#)" << "\n";
        }

        for(unsigned i = 0; i < num; i++) {
            std::string lname;
            bool flag = false;
            std::map<std::string, std::string> stat_map;

            if (map->m_mem_data[i].m_type != MEM_STREAMING)
                continue;

            ss << " [" << i << "] " << std::setw(16 - (std::to_string(i).length()) - 4) << std::left
               << map->m_mem_data[i].m_tag;

            ss << std::setw(8) << map->m_mem_data[i].route_id;
            ss << std::setw(5) << map->m_mem_data[i].flow_id;

            lname = std::string((char *)map->m_mem_data[i].m_tag);
            if (lname.back() == 'w'){
                lname = "route" + std::to_string(map->m_mem_data[i].route_id) + "/stat";
                flag = true;
            }
            else
                lname = "flow" + std::to_string(map->m_mem_data[i].flow_id) + "/stat";
            pcidev::get_dev(m_idx)->user->sysfs_get("str_dma", lname, errmsg, attrs);

            if (!errmsg.empty()) {
                ss << std::setw(10) << "Inactive";
                ss << std::setw(14) << "N/A" << std::setw(14) << "N/A" << std::setw(10) << "N/A";
            } else {
                ss << std::setw(10) << "Active";
                for (unsigned k = 0; k < attrs.size(); k++) {
                    char key[50];
                    int64_t value;

                    std::sscanf(attrs[k].c_str(), "%[^:]:%ld", key, &value);
                    stat_map[std::string(key)] = std::to_string(value);
                }

                ss << std::setw(14) << stat_map[std::string("total_req_bytes")] +

                    "/" + stat_map[std::string("total_req_num")];

                ss << std::setw(14) << stat_map[std::string("total_complete_bytes")] +
                    "/" + stat_map[std::string("total_complete_num")];

                pcidev::get_dev(m_idx)->user->sysfs_get("wq2", lname, errmsg, attrs);
                if (flag) {

                    int write_pending = ((std::stoi(stat_map[std::string("descq_pidx")]) - std::stoi(stat_map[std::string("descq_cidx")])) &
                                        (std::stoi(stat_map[std::string("descq_rngsz")])- 1))*4096;

                    ss << std::setw(10) << std::to_string(write_pending) ;

                }

                pcidev::get_dev(m_idx)->user->sysfs_get("rq2", lname, errmsg, attrs);
                if (!flag) {

                    int read_pending = ((std::stoi(stat_map[std::string("c2h_wrb_pidx")]) - std::stoi(stat_map[std::string("descq_cidx_wrb_pend")])) &
                                       (std::stoi(stat_map[std::string("descq_rngsz")]) - 1))*4096;
                    ss << std::setw(10) << std::to_string(read_pending);

                }
            }
            ss << "\n";

        }


        lines.push_back(ss.str());
    }

    int readSensors( void ) const
    {
        sensor_tree::put( "runtime.build.version",   xrt_build_version );
        sensor_tree::put( "runtime.build.hash",      xrt_build_version_hash );
        sensor_tree::put( "runtime.build.hash_date", xrt_build_version_hash_date );
        sensor_tree::put( "runtime.build.branch",    xrt_build_version_branch );
        // info
        sensor_tree::put( "board.info.dsa_name", m_devinfo.mName );
        sensor_tree::put( "board.info.vendor", m_devinfo.mVendorId );
        sensor_tree::put( "board.info.device", m_devinfo.mDeviceId );
        sensor_tree::put( "board.info.subdevice", m_devinfo.mSubsystemId );
        sensor_tree::put( "board.info.subvendor", m_devinfo.mSubsystemVendorId );
        sensor_tree::put( "board.info.xmcversion", m_devinfo.mXMCVersion );
        sensor_tree::put( "board.info.ddr_size", m_devinfo.mDDRSize );
        sensor_tree::put( "board.info.ddr_count", m_devinfo.mDDRBankCount );
        sensor_tree::put( "board.info.clock0", m_devinfo.mOCLFrequency[0] );
        sensor_tree::put( "board.info.clock1", m_devinfo.mOCLFrequency[1] );
        sensor_tree::put( "board.info.pcie_speed", m_devinfo.mPCIeLinkSpeed );
        sensor_tree::put( "board.info.pcie_width", m_devinfo.mPCIeLinkWidth );
        sensor_tree::put( "board.info.dma_threads", m_devinfo.mDMAThreads );
        sensor_tree::put( "board.info.mig_calibrated", m_devinfo.mMigCalib );
        //sensor_tree::put( "board.info.dna",

        // physical
        sensor_tree::put( "board.physical.thermal.pcb.top_front",                m_devinfo.mSE98Temp[ 0 ] );
        sensor_tree::put( "board.physical.thermal.pcb.top_rear",                 m_devinfo.mSE98Temp[ 1 ] );
        sensor_tree::put( "board.physical.thermal.pcb.btm_front",                m_devinfo.mSE98Temp[ 2 ] );
        sensor_tree::put( "board.physical.thermal.fpga_temp",                    m_devinfo.mOnChipTemp );
        sensor_tree::put( "board.physical.thermal.tcrit_temp",                   m_devinfo.mFanTemp );
        sensor_tree::put( "board.physical.thermal.fan_speed",                    m_devinfo.mFanRpm );
        sensor_tree::put( "board.physical.electrical.12v_pex.voltage",           m_devinfo.m12VPex );
        sensor_tree::put( "board.physical.electrical.12v_pex.current",           m_devinfo.mPexCurr );
        sensor_tree::put( "board.physical.electrical.12v_aux.voltage",           m_devinfo.m12VAux );
        sensor_tree::put( "board.physical.electrical.12v_aux.current",           m_devinfo.mAuxCurr );
        sensor_tree::put( "board.physical.electrical.3v3_pex.voltage",           m_devinfo.m3v3Pex );
        sensor_tree::put( "board.physical.electrical.3v3_aux.voltage",           m_devinfo.m3v3Aux );
        sensor_tree::put( "board.physical.electrical.ddr_vpp_bottom.voltage",    m_devinfo.mDDRVppBottom );
        sensor_tree::put( "board.physical.electrical.ddr_vpp_top.voltage",       m_devinfo.mDDRVppTop );
        sensor_tree::put( "board.physical.electrical.sys_5v5.voltage",           m_devinfo.mSys5v5 );
        sensor_tree::put( "board.physical.electrical.1v2_top.voltage",           m_devinfo.m1v2Top );
        sensor_tree::put( "board.physical.electrical.1v8_top.voltage",           m_devinfo.m1v8Top );
        sensor_tree::put( "board.physical.electrical.0v85.voltage",              m_devinfo.m0v85 );
        sensor_tree::put( "board.physical.electrical.mgt_0v9.voltage",           m_devinfo.mMgt0v9 );
        sensor_tree::put( "board.physical.electrical.12v_sw.voltage",            m_devinfo.m12vSW );
        sensor_tree::put( "board.physical.electrical.mgt_vtt.voltage",           m_devinfo.mMgtVtt );
        sensor_tree::put( "board.physical.electrical.vccint.voltage",            m_devinfo.mVccIntVol );
        sensor_tree::put( "board.physical.electrical.vccint.current",            m_devinfo.mVccIntCurr );

        // firewall
        unsigned i = m_errinfo.mFirewallLevel;
        sensor_tree::put( "board.error.firewall.firewall_level", m_errinfo.mFirewallLevel );
        sensor_tree::put( "board.error.firewall.status", parseFirewallStatus( m_errinfo.mAXIErrorStatus[ i ].mErrFirewallStatus ) );

        // memory
        getMemTopology();
        xclDeviceUsage devstat = { 0 };
        (void) xclGetUsageInfo(m_handle, &devstat);
        for (unsigned i = 0; i < 2; i++) {
            boost::property_tree::ptree pt_dma;
            pt_dma.put( "index", i );
            pt_dma.put( "h2c", unitConvert(devstat.h2c[i]) );
            pt_dma.put( "c2h", unitConvert(devstat.c2h[i]) );
            sensor_tree::add_child( "board.pcie_dma.transfer_metrics.chan", pt_dma );
        }
        // stream

        // xclbin
        std::string errmsg, xclbinid;
        pcidev::get_dev(m_idx)->user->sysfs_get("", "uid", errmsg, xclbinid);
        if(errmsg.empty()) {
            sensor_tree::put( "board.xclbin.id", xclbinid );
        }

        // compute unit
        std::vector<ip_data> computeUnits;
        if( getComputeUnits( computeUnits ) < 0 ) {
            std::cout << "WARNING: 'ip_layout' invalid. Has the bitstream been loaded? See 'xbutil program'.\n";
        }
        parseComputeUnits( computeUnits );

        return 0;
    }

    /*
     * dumpJson
     */
    int dumpJson(std::ostream& ostr) const
    {
        readSensors();
        sensor_tree::json_dump( ostr );
        return 0;
    }

    /*
     * dump
     *
     * TODO: Refactor to make function much shorter.
     */
    int dump(std::ostream& ostr) const {
        readSensors();
        ostr << std::left;
        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        ostr << "XRT\n   Version: " << sensor_tree::get( "runtime.build.version", "N/A" )
             <<    "\n   Date:    " << sensor_tree::get( "runtime.build.hash_date", "N/A" )
             <<    "\n   Hash:    " << sensor_tree::get( "runtime.build.hash", "N/A" ) << std::endl;
        ostr << "DSA name\n" << sensor_tree::get( "board.info.dsa_name", "N/A" ) << std::endl;
        ostr << std::setw(16) << "Vendor" << std::setw(16) << "Device" << std::setw(16) << "SubDevice" << std::setw(16) << "SubVendor" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.info.vendor", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.device", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.subdevice", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.subvendor", "N/A" ) << std::endl;
        ostr << std::setw(16) << "DDR size" << std::setw(16) << "DDR count" << std::setw(16) << "OCL Frequency" << std::setw(16) << "Clock0" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.info.ddr_size", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.ddr_count", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.ocl_freq", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.clock0", "N/A" ) << std::endl;
        ostr << std::setw(16) << "PCIe"
             << std::setw(16) << "DMA bi-directional threads"
             << std::setw(16) << "MIG Calibrated" << std::endl;
        ostr << "GEN " << sensor_tree::get( "board.info.pcie_speed", "N/A" ) << "x" << std::setw(10) << sensor_tree::get( "board.info.pcie_width", "N/A" )
             << std::setw(32) << sensor_tree::get( "board.info.dma_threads", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.info.mig_calibrated", "N/A" ) << std::endl;
        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        ostr << "Temperature (C):\n";
        ostr << std::setw(16) << "PCB TOP FRONT" << std::setw(16) << "PCB TOP REAR" << std::setw(16) << "PCB BTM FRONT" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.thermal.pcb.top_front", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.thermal.pcb.top_rear", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.thermal.pcb.btm_front", "N/A" ) << std::endl;
        ostr << std::setw(16) << "FPGA TEMP" << std::setw(16) << "TCRIT Temp" << std::setw(16) << "FAN Speed (RPM)" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.thermal.fpga_temp", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.thermal.tcrit_temp", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.thermal.fan_speed_rpm", "N/A" ) << std::endl;
        ostr << "Electrical (mV), (mA):\n";
        ostr << std::setw(16) << "12V PEX" << std::setw(16) << "12V AUX" << std::setw(16) << "12V PEX Current" << std::setw(16) << "12V AUX Current" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.electrical.12v_pex.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.12v_aux.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.12v_pex.current", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.12v_aux.current", "N/A" ) << std::endl;
        ostr << std::setw(16) << "3V3 PEX" << std::setw(16) << "3V3 AUX" << std::setw(16) << "DDR VPP BOTTOM" << std::setw(16) << "DDR VPP TOP" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.electrical.3v3_pex.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.3v3_aux.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.ddr_vpp_bottom.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.ddr_vpp_top.voltage", "N/A" ) << std::endl;
        ostr << std::setw(16) << "SYS 5V5" << std::setw(16) << "1V2 TOP" << std::setw(16) << "1V8 TOP" << std::setw(16) << "0V85" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.electrical.sys_v5v.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.1v2_top.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.1v8_top.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.0v85.voltage", "N/A" ) << std::endl;
        ostr << std::setw(16) << "MGT 0V9" << std::setw(16) << "12V SW" << std::setw(16) << "MGT VTT" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.electrical.mgt_0v9.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.12v_sw.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.mgt_vtt.voltage", "N/A" ) << std::endl;
        ostr << std::setw(16) << "VCCINT VOL" << std::setw(16) << "VCCINT CURR" << std::setw(16) << "DNA" << std::endl;
        ostr << std::setw(16) << sensor_tree::get( "board.physical.electrical.vccint.voltage", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.vccint.current", "N/A" )
             << std::setw(16) << sensor_tree::get( "board.physical.electrical.dna", "N/A" ) << std::endl;
        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        ostr << "Firewall Last Error Status:\n";
        ostr << " Level " << std::setw(2) << sensor_tree::get( "board.error.firewall.firewall_level", "N/A" ) << ": 0x0"
             << sensor_tree::get( "board.error.firewall.status", "N/A" ) << std::endl;
        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        ostr << std::left << std::setw(48) << "Mem Topology"
             << std::setw(32) << "Device Memory Usage" << std::endl;
        ostr << std::setw(16) << "Tag"  << std::setw(12) << "Type"
             << std::setw(12) << "Temp" << std::setw(8) << "Size";
        ostr << std::setw(16) << "Mem Usage" << std::setw(8) << "BO nums" << std::endl;

        try {
          for (auto& v : sensor_tree::get_child("board.memory")) {
            if( v.first == "mem" ) {
              int mem_index = -1;
              int mem_used = -1;
              std::string mem_tag = "N/A";
              std::string mem_size = "N/A";
              std::string mem_type = "N/A";
              std::string val;
              for (auto& subv : v.second) {
                val = subv.second.get_value<std::string>();
                if( subv.first == "index" )
                  mem_index = subv.second.get_value<int>();
                else if( subv.first == "type" )
                  mem_type = val;
                else if( subv.first == "tag" )
                  mem_tag = val;
                else if( subv.first == "used" )
                  mem_used = subv.second.get_value<int>();
                else if( subv.first == "size" )
                  mem_size = val;
              }
              ostr << std::left
                   << std::setw(2) << "[" << mem_index << "] "
                   << std::left << std::setw(14) << mem_tag
                   << std::setw(12) << " " << mem_type << " "
                   << std::setw(12) << mem_size << " "
                   << std::setw(16) << mem_used << std::endl;
            }
          }
        }
        catch( std::exception const& e) {
          // eat the exception, probably bad path
        }

        ostr << "Total DMA Transfer Metrics:" << std::endl;
        try {
          for (auto& v : sensor_tree::get_child( "board.pcie_dma.transfer_metrics" )) {
            if( v.first == "chan" ) {
              std::string chan_index, chan_h2c, chan_c2h, chan_val = "N/A";
              for (auto& subv : v.second ) {
                chan_val = subv.second.get_value<std::string>();
                if( subv.first == "index" )
                  chan_index = chan_val;
                else if( subv.first == "h2c" )
                  chan_h2c = chan_val;
                else if( subv.first == "c2h" )
                  chan_c2h = chan_val;
              }
              ostr << "  Chan[" << chan_index << "].h2c:  " << chan_h2c << std::endl;
              ostr << "  Chan[" << chan_index << "].c2h:  " << chan_c2h << std::endl;
            }
          }
        }
        catch( std::exception const& e) {
          // eat the exception, probably bad path
        }
        /* TODO: Stream topology and xclbin id. */
//        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
//        ostr << "Stream Topology, TODO\n";
//        printStreamInfo(ostr);
//        ostr << "#################################\n";
//        ostr << "XCLBIN ID:\n";
//        ostr << sensor_tree::get( "board.xclbin.uid", "0" ) << std::endl;
        ostr << "~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
        ostr << "Compute Unit Status:\n";
        try {
          for (auto& v : sensor_tree::get_child( "board.compute_unit" )) {
            if( v.first == "cu" ) {
              std::string val, cu_i, cu_n, cu_ba, cu_s = "N/A";
              for (auto& subv : v.second) {
                val = subv.second.get_value<std::string>();
                if( subv.first == "count" )
                  cu_i = val;
                else if( subv.first == "name" )
                  cu_n = val;
                else if( subv.first == "base_address" )
                  cu_ba = val;
                else if( subv.first == "status" )
                  cu_s = val;
              }
              ostr << std::setw(6) << "CU[" << cu_i << "]: "
                   << std::setw(16) << cu_n
                   << std::setw(7) << "@0x" << std::hex << cu_ba << " "
                   << std::setw(10) << cu_s << std::endl;
            }
          }
        }
        catch( std::exception const& e) {
            // eat the exception, probably bad path
        }
        return 0;
    }


    /*
     * print stream topology
     */
    int printStreamInfo(std::ostream& ostr) const {
        std::vector<std::string> usage_lines;
        m_stream_usage_stringize_dynamics(m_devinfo, usage_lines);
        return 0;
    }

    /*
     * program
     */
    int program(const std::string& xclbin, unsigned region) {
        std::ifstream stream(xclbin.c_str());

        if(!stream.is_open()) {
            std::cout << "ERROR: Cannot open " << xclbin << ". Check that it exists and is readable." << std::endl;
            return -ENOENT;
        }

        if(region) {
            std::cout << "ERROR: Not support other than -r 0 " << std::endl;
            return -EINVAL;
        }

        char temp[8];
        stream.read(temp, 8);

        if (std::strncmp(temp, "xclbin0", 8)) {
            if (std::strncmp(temp, "xclbin2", 8))
                return -EINVAL;
        }


        stream.seekg(0, stream.end);
        int length = stream.tellg();
        stream.seekg(0, stream.beg);

        char *buffer = new char[length];
        stream.read(buffer, length);
        const xclBin *header = (const xclBin *)buffer;
        int result = xclLockDevice(m_handle);
        if (result)
            return result;
        result = xclLoadXclBin(m_handle, header);
        delete [] buffer;
        (void) xclUnlockDevice(m_handle);

        return result;
    }

    /*
     * boot
     *
     * Boot requires root privileges. Boot calls xclBootFPGA given the device handle.
     * The device is closed and a re-enumeration of devices is performed. After, the
     * device is created again by calling xclOpen(). This cannot be done inside
     * xclBootFPGA because of scoping issues in m_handle, so it is done within boot().
     * Check m_handle as a valid pointer before returning.
     */
    int boot() {
        if (getuid() && geteuid()) {
            std::cout << "ERROR: boot operation requires root privileges" << std::endl; // todo move this to a header of common messages
            return -EACCES;
        } else {
            int retVal = -1;
            retVal = xclBootFPGA(m_handle);
            if( retVal == 0 )
            {
                m_handle = xclOpen( m_idx, nullptr, XCL_QUIET );
                ( m_handle != nullptr ) ? retVal = 0 : retVal = -1;
            }
            return retVal;
        }
    }

    int reset(unsigned region) {
        const xclResetKind kind = (region == 0xffffffff) ? XCL_RESET_FULL : XCL_RESET_KERNEL;
        return xclResetDevice(m_handle, kind);
    }

    int run(unsigned region, unsigned cu) {
        std::cout << "ERROR: Not implemented\n";
        return -1;
    }

    int fan(unsigned speed) {
        std::cout << "ERROR: Not implemented\n";
        return -1;
    }

    /*
     * dmatest
     *
     * TODO: Refactor this function to be much shorter.
     */
    int dmatest(size_t blockSize, bool verbose) {
        if (blockSize == 0)
            blockSize = 256 * 1024 * 1024; // Default block size

        if (verbose)
            std::cout << "Total DDR size: " << m_devinfo.mDDRSize/(1024 * 1024) << " MB\n";

        bool isAREDevice = false;
        if (strstr(m_devinfo.mName, "-xare")) {//This is ARE device
            isAREDevice = true;
        }

        int result = 0;
        unsigned long long addr = 0x0;
        unsigned long long sz = 0x1;
        unsigned int pattern = 'J';

        // get DDR bank count from mem_topology if possible
        std::string errmsg;
        std::vector<char> buf;

        pcidev::get_dev(m_idx)->user->sysfs_get(
            "", "mem_topology", errmsg, buf);
        if (!errmsg.empty()) {
            std::cout << errmsg << std::endl;
            return -EINVAL;
        }
        const mem_topology *map = (mem_topology *)buf.data();

        if(buf.empty() || map->m_count == 0) {
            std::cout << "WARNING: 'mem_topology' invalid, "
                << "unable to perform DMA Test. Has the bitstream been loaded? "
                << "See 'xbutil program'." << std::endl;
            return -EINVAL;
        }

        if (verbose)
            std::cout << "Reporting from mem_topology:" << std::endl;

        for(int32_t i = 0; i < map->m_count; i++) {
            if(map->m_mem_data[i].m_type == MEM_STREAMING)
                continue;

            if(map->m_mem_data[i].m_used) {
                if (verbose) {
                    std::cout << "Data Validity & DMA Test on "
                        << map->m_mem_data[i].m_tag << "\n";
                }
                addr = map->m_mem_data[i].m_base_address;

                for(unsigned sz = 1; sz <= 256; sz *= 2) {
                    result = memwriteQuiet(addr, sz, pattern);
                    if( result < 0 )
                        return result;
                    result = memreadCompare(addr, sz, pattern , false);
                    if( result < 0 )
                        return result;
                }
                DMARunner runner( m_handle, blockSize, i);
                result = runner.run();
            }
        }

        if (isAREDevice) {//This is ARE device
            //XARE Status Reg Base Addr = 0x90000
            //XARE Channel Up Addr is = 0x90010 (& 0x98010)
            // 32 bits = 0x2 means clock is up but channel is down
            // 32 bits = 0x3 mean clocks and channel both are up..
            //??? Sarab: Also check if link channel is up;
            //After that see if we should do one hope or more hops..

            //Raw Read/Write Delay Check
            unsigned numIteration = 10000;
            //addr = 0xC00000000;//48GB = 3 hops
            addr = 0x400000000;//16GB = one hop
            sz = 0x20000;//128KB
            long numHops = addr / m_devinfo.mDDRSize;
            auto t1 = Clock::now();
            for (unsigned i = 0; i < numIteration; i++) {
                memwriteQuiet(addr, sz, pattern);
            }
            auto t2 = Clock::now();
            auto timeARE = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();

            addr = 0x0;
            sz = 0x1;
            t1 = Clock::now();
            for (unsigned i = 0; i < numIteration; i++) {
                memwriteQuiet(addr, sz, pattern);
            }
            t2 = Clock::now();
            auto timeDDR = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();
            long delayPerHop = (timeARE - timeDDR) / (numIteration * numHops);
            std::cout << "Averaging ARE hardware latency over " << numIteration * numHops << " hops\n";
            std::cout << "Latency per ARE hop for 128KB: " << delayPerHop << " ns\n";
            std::cout << "Total latency over ARE: " << (timeARE - timeDDR) << " ns\n";
        }
        return result;
    }

    int memread(std::string aFilename, unsigned long long aStartAddr = 0, unsigned long long aSize = 0) {
        if (strstr(m_devinfo.mName, "-xare")) {//This is ARE device
          if (aStartAddr > m_devinfo.mDDRSize) {
              std::cout << "Start address " << std::hex << aStartAddr <<
                           " is over ARE" << std::endl;
          }
          if (aSize > m_devinfo.mDDRSize || aStartAddr+aSize > m_devinfo.mDDRSize) {
              std::cout << "Read size " << std::dec << aSize << " from address 0x" << std::hex << aStartAddr <<
                           " is over ARE" << std::endl;
          }
        }
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).read(
            aFilename, aStartAddr, aSize);
    }


    int memDMATest(size_t blocksize, unsigned int aPattern = 'J') {
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).runDMATest(
            blocksize, aPattern);
    }

    int memreadCompare(unsigned long long aStartAddr = 0, unsigned long long aSize = 0, unsigned int aPattern = 'J', bool checks = true) {
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).readCompare(
            aStartAddr, aSize, aPattern, checks);
    }

    int memwrite(unsigned long long aStartAddr, unsigned long long aSize, unsigned int aPattern = 'J') {
        if (strstr(m_devinfo.mName, "-xare")) {//This is ARE device
            if (aStartAddr > m_devinfo.mDDRSize) {
                std::cout << "Start address " << std::hex << aStartAddr <<
                             " is over ARE" << std::endl;
            }
            if (aSize > m_devinfo.mDDRSize || aStartAddr+aSize > m_devinfo.mDDRSize) {
                std::cout << "Write size " << std::dec << aSize << " from address 0x" << std::hex << aStartAddr <<
                             " is over ARE" << std::endl;
            }
        }
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).write(
            aStartAddr, aSize, aPattern);
    }

    int memwrite( unsigned long long aStartAddr, unsigned long long aSize, char *srcBuf )
    {
        if( strstr( m_devinfo.mName, "-xare" ) ) { //This is ARE device
            if( aStartAddr > m_devinfo.mDDRSize ) {
                std::cout << "Start address " << std::hex << aStartAddr <<
                             " is over ARE" << std::endl;
            }
            if( aSize > m_devinfo.mDDRSize || aStartAddr + aSize > m_devinfo.mDDRSize ) {
                std::cout << "Write size " << std::dec << aSize << " from address 0x" << std::hex << aStartAddr <<
                             " is over ARE" << std::endl;
            }
        }
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).write(
            aStartAddr, aSize, srcBuf);
    }

    int memwriteQuiet(unsigned long long aStartAddr, unsigned long long aSize, unsigned int aPattern = 'J') {
        return memaccess(m_handle, m_devinfo.mDDRSize, m_devinfo.mDataAlignment,
            pcidev::get_dev(m_idx)->user->sysfs_name).writeQuiet(
            aStartAddr, aSize, aPattern);
    }


   //Debug related functionality.
    uint32_t getIPCountAddrNames(int type, std::vector<uint64_t> *baseAddress, std::vector<std::string> * portNames);

    std::pair<size_t, size_t> getCUNamePortName (std::vector<std::string>& aSlotNames,
                             std::vector< std::pair<std::string, std::string> >& aCUNamePortNames);
    int readSPMCounters();
    int readSSPMCounters();
    int readLAPCheckers(int aVerbose);
    int print_debug_ip_list (int aVerbose);

    /*
     * do_dd
     *
     * Perform block read or writes to-device-from-file or from-device-to-file.
     *
     * Usage:
     * dd -d0 --if=in.txt --bs=4096 --count=16 --seek=10
     * dd -d0 --of=out.txt --bs=1024 --count=4 --skip=2
     * --if : specify the input file, if specified, direction is fileToDevice
     * --of : specify the output file, if specified, direction is deviceToFile
     * --bs : specify the block size OPTIONAL defaults to value specified in 'dd.h'
     * --count : specify the number of blocks to copy
     *           OPTIONAL for fileToDevice; will copy the remainder of input file by default
     *           REQUIRED for deviceToFile
     * --skip : specify the source offset (in block counts) OPTIONAL defaults to 0
     * --seek : specify the destination offset (in block counts) OPTIONAL defaults to 0
     */
    int do_dd(dd::ddArgs_t args )
    {
        if( !args.isValid ) {
            return -1; // invalid arguments
        }
        if( args.dir == dd::unset ) {
            return -1; // direction invalid
        } else if( args.dir == dd::deviceToFile ) {
            unsigned long long addr = args.skip; // ddr read offset
            while( args.count-- > 0 ) { // writes all full blocks
                memread( args.file, addr, args.blockSize ); // returns 0 on complete read.
                // how to check for partial reads when device is empty?
                addr += args.blockSize;
            }
        } else if( args.dir == dd::fileToDevice ) {
            // write entire contents of file to device DDR at seek offset.
            unsigned long long addr = args.seek; // ddr write offset
            std::ifstream iStream( args.file.c_str(), std::ifstream::binary );
            if( !iStream ) {
                perror( "open input file" );
                return errno;
            }
            // If unspecified count, calculate the count from the full file size.
            if( args.count <= 0 ) {
                iStream.seekg( 0, iStream.end );
                int length = iStream.tellg();
                args.count = length / args.blockSize + 1; // round up
                iStream.seekg( 0, iStream.beg );
            }
            iStream.seekg( 0, iStream.beg );

            char *buf;
            static char *inBuf;
            size_t inSize;

            inBuf = (char*)malloc( args.blockSize );
            if( !inBuf ) {
                perror( "malloc block size" );
                return errno;
            }

            while( args.count-- > 0 ) { // writes all full blocks
                buf = inBuf;
                inSize = iStream.read( inBuf, args.blockSize ).gcount();
                if( (int)inSize == args.blockSize ) {
                    // full read
                } else {
                    // Partial read--write size specified greater than read size. Writing remainder of input file.
                    args.count = 0; // force break
                }
                memwrite( addr, inSize, buf );
                addr += inSize;
            }
            iStream.close();
        }
        return 0;
    }

    int usageInfo(xclDeviceUsage& devstat) const {
        return xclGetUsageInfo(m_handle, &devstat);
    }

    int deviceInfo(xclDeviceInfo2& devinfo) const {
        return xclGetDeviceInfo2(m_handle, &devinfo);
    }

    int validate(bool quick);

    int printEccInfo(std::ostream& ostr) const;
    int resetEccInfo();

private:
    // Run a test case as <exe> <xclbin> [-d index] on this device and collect
    // all output from the run into "output"
    // Note: exe should assume index to be 0 without -d
    int runTestCase(const std::string& exe, const std::string& xclbin,
        std::string& output);
};

void printHelp(const std::string& exe);
int xclTop(int argc, char *argv[], xcldev::subcommand subcmd);
int xclValidate(int argc, char *argv[]);
std::unique_ptr<xcldev::device> xclGetDevice(unsigned index);
} // end namespace xcldev

#endif /* XBUTIL_H */
