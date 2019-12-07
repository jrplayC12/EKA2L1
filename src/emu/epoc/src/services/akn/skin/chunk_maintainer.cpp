#include <epoc/services/akn/skin/chunk_maintainer.h>
#include <epoc/services/akn/skin/skn.h>
#include <epoc/kernel/chunk.h>
#include <common/path.h>

namespace eka2l1::epoc {
    constexpr std::int64_t AKNS_CHUNK_ITEM_DEF_HASH_BASE_SIZE_GRAN = -4;
    constexpr std::int64_t AKNS_CHUNK_ITEM_DEF_AREA_BASE_SIZE_GRAN = 11;
    constexpr std::int64_t AKNS_CHUNK_DATA_AREA_BASE_SIZE_GRAN = 20;
    constexpr std::int64_t AKNS_CHUNK_FILENAME_AREA_BASE_SIZE_GRAN = 1;
    constexpr std::int64_t AKNS_CHUNK_SCALEABLE_GFX_AREA_BASE_SIZE_GRAN = 1;

    // ======================= DEFINITION FOR CHUNK STRUCT =======================
    struct akns_srv_bitmap_def {
        akns_mtptr filename_;           ///< Pointer to filename.
        std::int32_t index_;            ///< Index in bitmap file.
        std::int32_t image_attrib_;     ///< Attribute for the bitmap.
        std::int32_t image_alignment_;  ///< Alignment for the bitmap.
        std::int32_t image_x_coord_;    ///< X coordinate of the image.
        std::int32_t image_y_coord_;    ///< Y coordinate of the image.
        std::int32_t image_width_;      ///< Width of the image.
        std::int32_t image_height_;     ///< Height of the image.
    };

    struct akns_srv_masked_bitmap_def {
        akns_mtptr filename_;           ///< Pointer to filename.
        std::int32_t index_;            ///< Index in bitmap file.
        std::int32_t masked_index_;     ///< Masked index in bitmap file.
        std::int32_t image_attrib_;     ///< Attribute for the bitmap.
        std::int32_t image_alignment_;  ///< Alignment for the bitmap.
        std::int32_t image_x_coord_;    ///< X coordinate of the image.
        std::int32_t image_y_coord_;    ///< Y coordinate of the image.
        std::int32_t image_width_;      ///< Width of the image.
        std::int32_t image_height_;     ///< Height of the image.
    };

    static pid make_pid_from_id_hash(const std::uint64_t hash) {
        return { static_cast<std::int32_t>(hash >> 32), static_cast<std::int32_t>(hash) };
    }

    akn_skin_chunk_maintainer::akn_skin_chunk_maintainer(kernel::chunk *shared_chunk, const std::size_t granularity)
        : shared_chunk_(shared_chunk)
        , current_granularity_off_(0)
        , max_size_gran_(0)
        , granularity_(granularity)
        , level_(0) {
        // Calculate max size this chunk can hold (of course, in granularity meters)
        max_size_gran_ = shared_chunk_->max_size() / granularity;

        // Add areas according to normal configuration
        add_area(akn_skin_chunk_area_base_offset::item_def_hash_base, AKNS_CHUNK_ITEM_DEF_HASH_BASE_SIZE_GRAN);
        add_area(akn_skin_chunk_area_base_offset::item_def_area_base, AKNS_CHUNK_ITEM_DEF_AREA_BASE_SIZE_GRAN);
        add_area(akn_skin_chunk_area_base_offset::data_area_base, AKNS_CHUNK_DATA_AREA_BASE_SIZE_GRAN);
        add_area(akn_skin_chunk_area_base_offset::gfx_area_base, AKNS_CHUNK_SCALEABLE_GFX_AREA_BASE_SIZE_GRAN);
        add_area(akn_skin_chunk_area_base_offset::filename_area_base, AKNS_CHUNK_FILENAME_AREA_BASE_SIZE_GRAN);

        // Fill hash area with negaitve
        std::uint8_t *area = reinterpret_cast<std::uint8_t*>(get_area_base(akn_skin_chunk_area_base_offset::item_def_hash_base));
        std::fill(area, area + get_area_size(akn_skin_chunk_area_base_offset::item_def_hash_base), 0xFF);
    }

    const std::uint32_t akn_skin_chunk_maintainer::maximum_filename() {
        const std::size_t area_size = get_area_size(akn_skin_chunk_area_base_offset::filename_area_base);

        // If the filename area doesn't exist
        if (area_size == static_cast<std::size_t>(-1)) {
            return static_cast<std::uint32_t>(-1);
        }

        return static_cast<std::uint32_t>(area_size / AKN_SKIN_SERVER_MAX_FILENAME_BYTES);
    }

    const std::uint32_t akn_skin_chunk_maintainer::current_filename_count() {
        const std::size_t area_crr_size = get_area_current_size(akn_skin_chunk_area_base_offset::filename_area_base);

        // If the filename area doesn't exist
        if (area_crr_size == static_cast<std::size_t>(-1)) {
            return static_cast<std::uint32_t>(-1);
        }

        return static_cast<std::uint32_t>(area_crr_size / AKN_SKIN_SERVER_MAX_FILENAME_BYTES);
    }

    static std::uint32_t *search_filename_in_area(std::uint32_t *area, const std::uint32_t filename_id, const std::uint32_t count) {
        if (area == nullptr) {
            // Area doesn't exist
            return nullptr;
        }

        // Do a smally search.
        for (std::uint32_t i = 0; i < count; i++) {
            if (area[0] == filename_id) {
                return area;
            }

            // We need to continue
            area += AKN_SKIN_SERVER_MAX_FILENAME_BYTES / 4;
        }

        return nullptr;
    }

    std::int32_t akn_skin_chunk_maintainer::get_filename_offset_from_id(const std::uint32_t filename_id) {
        std::uint32_t *areabase = reinterpret_cast<std::uint32_t*>(get_area_base(akn_skin_chunk_area_base_offset::filename_area_base));

        if (areabase == nullptr) {
            // Area doesn't exist
            return false;
        }

        const std::uint32_t crr_fn_count = current_filename_count();
        std::uint32_t *area_ptr = search_filename_in_area(areabase, filename_id, crr_fn_count);

        if (!area_ptr) {
            return -1;
        }

        return static_cast<std::int32_t>((area_ptr - areabase) * sizeof(std::uint32_t));
    }
    
    bool akn_skin_chunk_maintainer::update_filename(const std::uint32_t filename_id, const std::u16string &filename,
        const std::u16string &filename_base) {
        // We need to search for the one and only.
        // Get the base first
        std::uint32_t *areabase = reinterpret_cast<std::uint32_t*>(get_area_base(akn_skin_chunk_area_base_offset::filename_area_base));

        if (areabase == nullptr) {
            // Area doesn't exist
            return false;
        }

        const std::uint32_t crr_fn_count = current_filename_count();
        std::uint32_t *areaptr = search_filename_in_area(areabase, filename_id, crr_fn_count);

        // Check if we found the name?
        if (!areaptr) {
            areaptr = areabase + crr_fn_count * AKN_SKIN_SERVER_MAX_FILENAME_BYTES / 4;

            // Nope, expand the chunk and add this guy in
            set_area_current_size(akn_skin_chunk_area_base_offset::filename_area_base,
                static_cast<std::uint32_t>(get_area_current_size(akn_skin_chunk_area_base_offset::filename_area_base)
                + AKN_SKIN_SERVER_MAX_FILENAME_BYTES));
        }

        // Let's do copy!
        areaptr[0] = filename_id;
        areaptr += 4;

        // Copy the base in
        std::copy(filename_base.data(), filename_base.data() + filename_base.length(), reinterpret_cast<char16_t*>(areaptr));
        std::copy(filename.data(), filename.data() + filename.length(), reinterpret_cast<char16_t*>(
            reinterpret_cast<std::uint8_t*>(areaptr) + filename_base.length() * 2));

        // Fill 0 in the unused places
        std::fill(reinterpret_cast<std::uint8_t*>(areaptr) + filename_base.length() * 2 + filename.length() * 2,
            reinterpret_cast<std::uint8_t*>(areaptr) + AKN_SKIN_SERVER_MAX_FILENAME_BYTES, 0);

        return true;
    }

    static constexpr std::int32_t MAX_HASH_AVAIL = 128;

    static std::uint32_t calculate_item_hash(const epoc::pid &id) {
        return (id.first + id.second) % MAX_HASH_AVAIL;
    }

    std::int32_t akn_skin_chunk_maintainer::get_item_definition_index(const epoc::pid &id) {
        std::int32_t *hash = reinterpret_cast<std::int32_t*>(get_area_base(epoc::akn_skin_chunk_area_base_offset::item_def_hash_base));

        epoc::akns_item_def *defs = reinterpret_cast<decltype(defs)>(get_area_base(epoc::akn_skin_chunk_area_base_offset::item_def_area_base));
        std::uint32_t hash_index = calculate_item_hash(id);

        std::int32_t head = hash[hash_index];

        if (head < 0) {
            return -1;
        }

        while (head >= 0 && defs[head].id_ != id) {
            head = defs[head].next_hash_;
        }

        return head;
    }
    
    std::int32_t akn_skin_chunk_maintainer::update_data(const std::uint8_t *new_data, std::uint8_t *old_data, const std::size_t new_size, const std::size_t old_size) {
        std::int32_t offset = 0;

        if (old_data == nullptr || (old_size < new_size)) {
            // Just add the new data.
            offset = static_cast<std::int32_t>(get_area_current_size(epoc::akn_skin_chunk_area_base_offset::data_area_base));
            std::uint8_t *data_head = reinterpret_cast<std::uint8_t*>(get_area_base(epoc::akn_skin_chunk_area_base_offset::data_area_base));

            set_area_current_size(epoc::akn_skin_chunk_area_base_offset::data_area_base, 
                static_cast<std::uint32_t>(offset + new_size));

            std::copy(new_data, new_data + new_size, data_head);

            return offset;
        }

        // Just replace old data
        std::copy(new_data, new_data + new_size, old_data);
        return static_cast<std::int32_t>(old_data - reinterpret_cast<std::uint8_t*>(get_area_base(
            epoc::akn_skin_chunk_area_base_offset::data_area_base)));
    }
    
    // More efficient. Giving the index first.
    bool akn_skin_chunk_maintainer::update_definition_hash(epoc::akns_item_def *def, const std::int32_t index) {
        // Get current head
        std::int32_t head = calculate_item_hash(def->id_);
        std::int32_t *hash = reinterpret_cast<std::int32_t*>(get_area_base(epoc::akn_skin_chunk_area_base_offset::item_def_hash_base));

        if (head == 1) {
            int a = 5;
        }

        // Add the definition as the head
        def->next_hash_ = hash[head];

        // Get index of the defintion
        hash[head] = index;

        return true;
    }
    
    bool akn_skin_chunk_maintainer::update_definition(const epoc::akns_item_def &def, const void *data, const std::size_t data_size,
        const std::size_t old_data_size) {
        std::int32_t index = get_item_definition_index(def.id_);
        std::size_t old_data_size_to_update = 0;
        void *old_data = nullptr;
        akns_item_def *current_def = nullptr;

        if (index < 0) {
            const std::size_t def_size = get_area_current_size(epoc::akn_skin_chunk_area_base_offset::item_def_area_base);

            // Add this to the chunk immediately. No hesitation.
            std::uint8_t *current_head = reinterpret_cast<std::uint8_t*>(
                get_area_base(epoc::akn_skin_chunk_area_base_offset::item_def_area_base)) +
                def_size;

            set_area_current_size(epoc::akn_skin_chunk_area_base_offset::item_def_area_base,
                static_cast<std::uint32_t>(def_size + sizeof(akns_item_def)));

            std::memcpy(current_head, &def, sizeof(akns_item_def));

            // Update the hash
            update_definition_hash(reinterpret_cast<akns_item_def*>(current_head),
                static_cast<std::int32_t>(def_size / sizeof(akns_item_def)));

            current_def = reinterpret_cast<akns_item_def*>(current_head);
        } else {
            // The defintion already exists. Recopy it
            current_def = reinterpret_cast<akns_item_def*>(get_area_base(
                epoc::akn_skin_chunk_area_base_offset::item_def_area_base)) + index;

            if (current_def->type_ == def.type_ && current_def->data_.type_ == epoc::akns_mtptr_type::akns_mtptr_type_relative_ram) {
                std::uint32_t *data_size = current_def->data_.get_relative<std::uint32_t>(
                    get_area_base(epoc::akn_skin_chunk_area_base_offset::data_area_base));

                if (old_data_size == -1) {
                    old_data_size_to_update = *data_size;
                } else {
                    old_data_size_to_update = old_data_size;
                }

                old_data = data_size;
            }
            
            std::int32_t head = current_def->next_hash_;
            std::memcpy(current_def, &def, sizeof(akns_item_def));
            current_def->next_hash_ = head;
        }

        // Now we update data.
        current_def->data_.type_ = akns_mtptr_type_relative_ram;
        current_def->data_.address_or_offset_ = update_data(reinterpret_cast<const std::uint8_t*>(
            data), reinterpret_cast<std::uint8_t*>(old_data), data_size, old_data_size_to_update);

        return true;
    }
        
    bool akn_skin_chunk_maintainer::add_area(const akn_skin_chunk_area_base_offset offset_type, 
        const std::int64_t allocated_size_gran) {
        // Enum value "*_base" always aligned with 3
        if (static_cast<int>(offset_type) % 3 != 0) {
            return false;
        }

        akn_skin_chunk_area area;
        area.base_ = offset_type;
        area.gran_off_ = current_granularity_off_;
        area.gran_size_ = allocated_size_gran;

        // Check if the area exists before
        if (get_area_info(offset_type)) {
            // Cancel
            return false;
        }

        std::size_t gran_to_increase_off = static_cast<std::size_t>(std::max<std::int64_t>(1ULL,
            allocated_size_gran));

        // Check if memory is sufficient enough for this area.
        if (current_granularity_off_ + gran_to_increase_off > max_size_gran_) {
            return false;
        }

        // Get chunk base
        std::uint32_t *base = reinterpret_cast<std::uint32_t*>(shared_chunk_->host_base());

        if (!areas_.empty() && base[static_cast<int>(areas_[0].base_) + 1] <= 4 * 3) {
            // We can eat more first area memory for the header. Abort!
            return false;
        }

        // Set area base offset
        base[static_cast<int>(offset_type)] = static_cast<std::uint32_t>(current_granularity_off_ *
            granularity_);
        
        // Set area allocated size
        if (allocated_size_gran < 0) {
            base[static_cast<int>(offset_type) + 1] = static_cast<std::uint32_t>(granularity_ / 
                allocated_size_gran);
        } else {    
            base[static_cast<int>(offset_type) + 1] = static_cast<std::uint32_t>(allocated_size_gran * 
                granularity_);
        }

        // Clear area current size to 0
        base[static_cast<int>(offset_type) + 2] = 0;

        // Add the area to area list
        areas_.emplace_back(area);

        const std::uint32_t header_size = static_cast<std::uint32_t>(akn_skin_chunk_area_base_offset::base_offset_end) * 4;

        // We need to make offset align (for faster memory access)
        // Modify the first area offset to be after the header
        // Each area header contains 3 fields: offset, allocated size, current size, each field is 4 bytes
        // We also need to modify the size too.
        base[static_cast<int>(areas_[0].base_)] = header_size;
        base[static_cast<int>(areas_[0].base_) + 1] = static_cast<std::uint32_t>(
            get_area_size(areas_[0].base_, true) - header_size);

        current_granularity_off_ += gran_to_increase_off;
        return true;
    }

    akn_skin_chunk_maintainer::akn_skin_chunk_area *akn_skin_chunk_maintainer::get_area_info(const akn_skin_chunk_area_base_offset area_type) {
        if (static_cast<int>(area_type) % 3 != 0) {
            // Don't satisfy the condition
            return nullptr;
        }

        // Search for the area.
        auto search_result = std::find_if(areas_.begin(), areas_.end(),
            [area_type](const akn_skin_chunk_area &lhs) {
                return lhs.base_ == area_type;
            });

        if (search_result == areas_.end()) {
            return nullptr;
        }

        return &(*search_result);
    }
    
    const std::size_t akn_skin_chunk_maintainer::get_area_size(const akn_skin_chunk_area_base_offset area_type, const bool paper_calc) {
        akn_skin_chunk_area *area = get_area_info(area_type);

        if (!area) {
            // We can't get the area. Abort!!!
            return static_cast<std::size_t>(-1);
        }

        if (paper_calc) {
            if (area->gran_size_ < 0) {
                return granularity_ / (-1 * area->gran_size_);
            }

            return granularity_ * area->gran_size_;
        }
        
        std::uint32_t *base = reinterpret_cast<std::uint32_t*>(shared_chunk_->host_base());
        return static_cast<std::size_t>(base[static_cast<int>(area_type) + 1]);
    }

    void *akn_skin_chunk_maintainer::get_area_base(const akn_skin_chunk_area_base_offset area_type, 
        std::uint64_t *offset_from_begin) {
        akn_skin_chunk_area *area = get_area_info(area_type);

        if (!area) {
            // We can't get the area. Abort!!!
            return nullptr;
        }

        std::uint32_t *base = reinterpret_cast<std::uint32_t*>(shared_chunk_->host_base());

        // Check for the optional pointer, if valid, write the offset of the area from the beginning of the chunk
        // to it
        if (offset_from_begin) {
            *offset_from_begin = base[static_cast<int>(area_type)];
        }

        return reinterpret_cast<std::uint8_t*>(shared_chunk_->host_base()) + (base[static_cast<int>(area_type)]);
    }

    const std::size_t akn_skin_chunk_maintainer::get_area_current_size(const akn_skin_chunk_area_base_offset area_type) {
        akn_skin_chunk_area *area = get_area_info(area_type);

        if (!area) {
            // We can't get the area. Abort!!!
            return static_cast<std::size_t>(-1);
        }

        // Get chunk base
        std::uint32_t *base = reinterpret_cast<std::uint32_t*>(shared_chunk_->host_base());
        return static_cast<std::size_t>(base[static_cast<int>(area_type) + 2]);
    }

    bool akn_skin_chunk_maintainer::set_area_current_size(const akn_skin_chunk_area_base_offset area_type, const std::uint32_t new_size) {
         akn_skin_chunk_area *area = get_area_info(area_type);

        if (!area) {
            // We can't get the area. Abort!!!
            return false;
        }
        
        // Get chunk base
        std::uint32_t *base = reinterpret_cast<std::uint32_t*>(shared_chunk_->host_base());
        base[static_cast<int>(area_type) + 2] = new_size;

        return true;
    }

    bool akn_skin_chunk_maintainer::import_bitmap(const skn_bitmap_info &info) {
        akns_item_def item;
        item.type_ = akns_item_type_bitmap;
        item.id_ = make_pid_from_id_hash(info.id_hash);
        
        if (info.mask_bitmap_idx == -1) {
            akns_srv_bitmap_def bitmap_def;
            bitmap_def.filename_.type_ = akns_mtptr_type_relative_ram;
            bitmap_def.filename_.address_or_offset_ = get_filename_offset_from_id(info.filename_id);

            bitmap_def.index_ = info.bmp_idx;
            bitmap_def.image_alignment_ = info.attrib.align;
            bitmap_def.image_attrib_ = info.attrib.attrib;
            bitmap_def.image_height_ = info.attrib.image_size_y;
            bitmap_def.image_width_ = info.attrib.image_size_x;
            bitmap_def.image_x_coord_ = info.attrib.image_coord_x;
            bitmap_def.image_y_coord_ = info.attrib.image_coord_y;

            return update_definition(item, &bitmap_def, sizeof(bitmap_def), sizeof(bitmap_def));
        }

        akns_srv_masked_bitmap_def masked_bitmap_def;
        masked_bitmap_def.filename_.type_ = akns_mtptr_type_relative_ram;
        masked_bitmap_def.filename_.address_or_offset_ = get_filename_offset_from_id(info.filename_id);

        masked_bitmap_def.index_ = info.bmp_idx;
        masked_bitmap_def.masked_index_ = info.mask_bitmap_idx;
        masked_bitmap_def.image_alignment_ = info.attrib.align;
        masked_bitmap_def.image_attrib_ = info.attrib.attrib;
        masked_bitmap_def.image_height_ = info.attrib.image_size_y;
        masked_bitmap_def.image_width_ = info.attrib.image_size_x;
        masked_bitmap_def.image_x_coord_ = info.attrib.image_coord_x;
        masked_bitmap_def.image_y_coord_ = info.attrib.image_coord_y;

        return update_definition(item, &masked_bitmap_def, sizeof(masked_bitmap_def), sizeof(masked_bitmap_def));
    }

    bool akn_skin_chunk_maintainer::import(skn_file &skn, const std::u16string &filename_base) {
        // First up import filenames
        for (auto &filename: skn.filenames_) {
            if (!update_filename(filename.first, filename.second, filename_base)) {
                return false;
            }
        }

        // Import bitmap
        for (auto &bmp: skn.bitmaps_) {
            if (!import_bitmap(bmp.second)) {
                return false;
            }
        }

        return true;
    }
}