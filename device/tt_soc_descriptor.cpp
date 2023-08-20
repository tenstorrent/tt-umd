#include "tt_soc_descriptor.h"

#include <assert.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>

// #include "l1_address_map.h"
#include "yaml-cpp/yaml.h"

std::string format_node(tt_xy_pair xy) { return std::to_string(xy.x) + "-" + std::to_string(xy.y); }

tt_xy_pair format_node(std::string str) {
  int x_coord;
  int y_coord;
  std::regex expr("([0-9]+)[-,xX]([0-9]+)");
  std::smatch x_y_pair;

  if (std::regex_search(str, x_y_pair, expr)) {
    x_coord = std::stoi(x_y_pair[1]);
    y_coord = std::stoi(x_y_pair[2]);
  } else {
    throw std::runtime_error("Could not parse the core id: " + str);
  }

  tt_xy_pair xy(x_coord, y_coord);

  return xy;
}

int tt_SocDescriptor::get_num_dram_channels() const {
    int num_channels = 0;
    for (auto& dram_core : dram_cores) {
        if (dram_core.size() > 0) {
            num_channels++;
        }
    }
    return num_channels;
}

std::vector<int> tt_SocDescriptor::get_dram_chan_map() {
    std::vector<int> chan_map;
    for (unsigned int i = 0; i < dram_cores.size(); i++) {
        chan_map.push_back(i);
    }
    return chan_map;
};

bool tt_SocDescriptor::is_worker_core(const tt_xy_pair &core) const {
    return (
        routing_x_to_worker_x.find(core.x) != routing_x_to_worker_x.end() &&
        routing_y_to_worker_y.find(core.y) != routing_y_to_worker_y.end());
}

tt_xy_pair tt_SocDescriptor::get_worker_core(const tt_xy_pair &core) const {
    tt_xy_pair worker_xy = {
        static_cast<size_t>(routing_x_to_worker_x.at(core.x)), static_cast<size_t>(routing_y_to_worker_y.at(core.y))};
    return worker_xy;
}

tt_xy_pair tt_SocDescriptor::get_routing_core(const tt_xy_pair& core) const {
    tt_xy_pair routing_xy = {
        static_cast<size_t>(worker_log_to_routing_x.at(core.x)), static_cast<size_t>(worker_log_to_routing_y.at(core.y))};
    return routing_xy;
}

tt_xy_pair tt_SocDescriptor::get_core_for_dram_channel(int dram_chan, int subchannel) const {
    return this->dram_cores.at(dram_chan).at(subchannel);
};

tt_xy_pair tt_SocDescriptor::get_pcie_core(int pcie_id) const {
    return this->pcie_cores.at(pcie_id);
};

bool tt_SocDescriptor::is_ethernet_core(const tt_xy_pair &core) const {
    return this->ethernet_core_channel_map.find(core) != ethernet_core_channel_map.end();
}

int tt_SocDescriptor::get_channel_of_ethernet_core(const tt_xy_pair &core) const {
    return this->ethernet_core_channel_map.at(core);
}

int tt_SocDescriptor::get_num_dram_subchans() const {
    int num_chan = 0;
    for (const std::vector<tt_xy_pair> &core : this->dram_cores) {
        num_chan += core.size();
    }
    return num_chan;
}

int tt_SocDescriptor::get_num_dram_blocks_per_channel() const {
    int num_blocks = 0;
    if (arch == tt::ARCH::GRAYSKULL) {
        num_blocks = 1;
    } else if (arch == tt::ARCH::WORMHOLE) {
        num_blocks = 2;
    } else if (arch == tt::ARCH::WORMHOLE_B0) {
        num_blocks = 2;
    }
    return num_blocks;
}

uint64_t tt_SocDescriptor::get_noc2host_offset(uint16_t host_channel) const {

    const std::uint64_t PEER_REGION_SIZE = (1024 * 1024 * 1024);

    if (arch == tt::ARCH::GRAYSKULL) {
        return (host_channel * PEER_REGION_SIZE);
    }else if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        return (host_channel * PEER_REGION_SIZE) + 0x800000000;
    } else {
        throw std::runtime_error("Unsupported architecture");
    }
}

std::ostream &operator<<(std::ostream &out, const tt::ARCH &arch_name) {
    if (arch_name == tt::ARCH::JAWBRIDGE) {
        out << "jawbridge";
    } else if (arch_name == tt::ARCH::Invalid) {
        out << "none";
    } else if (arch_name == tt::ARCH::GRAYSKULL) {
        out << "grayskull";
    } else if (arch_name == tt::ARCH::WORMHOLE) {
        out << "wormhole";
    } else if (arch_name == tt::ARCH::WORMHOLE_B0) {
        out << "wormhole_b0";
    } else {
        out << "ArchNameSerializationNotImplemented";
    }

    return out;
}
