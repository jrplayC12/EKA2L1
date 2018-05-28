#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <memory>
#include <vector>

namespace YAML {
	class Node;
}

namespace eka2l1 {
	class io_system;

    using sid = uint32_t;
    using sids = std::vector<uint32_t>;
    using exportaddr = uint32_t;
    using exportaddrs = sids;

    typedef uint32_t address;

	enum class epocver {
		epoc6,
		epoc9
	};

	namespace loader {
		struct eka2img;
		struct romimg;

		using e32img_ptr = std::shared_ptr<eka2img>;
		using romimg_ptr = std::shared_ptr<romimg>;
	}

    namespace hle {
        // This class is launched at configuration time, so
        // no race condition.
        class lib_manager {
            std::map<std::string, sids> ids;
			std::map<sid, std::string> func_names;

            std::map<std::u16string, exportaddrs> exports;
            std::map<address, sid> addr_map;

			struct e32img_inf {
				loader::e32img_ptr img;
				bool is_xip;
			};
			
			// Caches the image
			std::map<uint32_t, e32img_inf> e32imgs_cache;
			std::map<uint32_t, loader::romimg_ptr> romimgs_cache;

			void load_all_sids(const epocver ver);

			io_system* io;

        public:
            lib_manager(const epocver ver);

            std::optional<sids> get_sids(const std::string& lib_name);
            std::optional<exportaddrs> get_export_addrs(const std::u16string& lib_name);

			// Image name
			loader::e32img_ptr load_e32img(const std::u16string& img_name);
			loader::romimg_ptr load_romimg(const std::u16string& rom_name);

			// Open the image code segment
			void open_e32img(loader::e32img_ptr& img);

            // Register export addresses for desired HLE library
            // This will also map the export address with the correspond SID
            // Note that these export addresses are unique, since they are the address in
            // the memory.
            bool register_exports(const std::u16string& lib_name, exportaddrs& addrs);
            std::optional<sid> get_sid(exportaddr addr);
        };
    }
}