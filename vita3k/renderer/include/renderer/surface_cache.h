#pragma once

#include <cstdint>
#include <vector>
#include <unordered_set>
#include <memory>

namespace renderer {

/**
 * @brief Efficient surface change tracking using hash set
 * 
 * Replaces previous O(n) vector scan with O(1) hash set lookup.
 * Critical optimization for games with heavy surface sync operations (e.g., God Eater Resurrection).
 */
class SurfaceChangeTracker {
public:
    /**
     * @brief Check if surface address has changed in this frame
     * @param address GPU surface address
     * @return true if address is already tracked as changed
     */
    bool is_dirty(uint64_t address) const {
        return dirty_surfaces.count(address) > 0;
    }
    
    /**
     * @brief Mark surface address as changed
     * @param address GPU surface address to mark dirty
     * @return true if newly inserted, false if already existed
     */
    bool mark_dirty(uint64_t address) {
        return dirty_surfaces.insert(address).second;
    }
    
    /**
     * @brief Batch mark multiple surfaces as changed
     * @param addresses Vector of GPU surface addresses
     */
    void mark_dirty_batch(const std::vector<uint64_t>& addresses) {
        for (uint64_t addr : addresses) {
            dirty_surfaces.insert(addr);
        }
    }
    
    /**
     * @brief Get count of dirty surfaces in current frame
     * @return Number of unique dirty surface addresses
     */
    size_t count_dirty() const {
        return dirty_surfaces.size();
    }
    
    /**
     * @brief Clear dirty surface tracking for next frame
     * Called at end of each render frame
     */
    void clear_frame() {
        dirty_surfaces.clear();
    }
    
    /**
     * @brief Check if any surfaces changed in this frame
     * @return true if dirty_surfaces is not empty
     */
    bool has_changes() const {
        return !dirty_surfaces.empty();
    }

private:
    // O(1) lookup/insert instead of O(n) vector scan
    std::unordered_set<uint64_t> dirty_surfaces;
};

} // namespace renderer