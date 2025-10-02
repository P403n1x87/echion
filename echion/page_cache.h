// This file is part of "echion" which is released under MIT.
//
// Copyright (c) 2023 Gabriele N. Tornetta <phoenix1987@gmail.com>.

#pragma once

#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>  // For std::find

// Page-based memory cache for reducing process_vm_readv system calls
// This cache stores 4KB pages of remote process memory to amortize 
// the cost of system calls across multiple small reads.

constexpr size_t PAGE_SIZE = 4096;
constexpr size_t MAX_CACHED_PAGES = 64;  // 256KB total cache
constexpr auto CACHE_TTL_MS = std::chrono::milliseconds(100);  // 100ms TTL

class PageMemoryCache {
public:
    struct CachedPage {
        uintptr_t page_addr;                              // Page-aligned address
        std::vector<uint8_t> data;                        // Page data (4KB)
        std::chrono::steady_clock::time_point timestamp;  // When cached
        bool valid;                                       // Is data valid?
        
        CachedPage() : page_addr(0), data(PAGE_SIZE), valid(false) {}
        
        CachedPage(uintptr_t addr) : page_addr(addr), data(PAGE_SIZE), valid(false) {
            timestamp = std::chrono::steady_clock::now();
        }
    };

private:
    mutable std::mutex cache_mutex;
    std::unordered_map<uintptr_t, std::unique_ptr<CachedPage>> pages;
    
    // LRU tracking for eviction
    std::vector<uintptr_t> lru_order;
    
    static inline uintptr_t page_align(uintptr_t addr) {
        return addr & ~(PAGE_SIZE - 1);
    }
    
    static inline size_t page_offset(uintptr_t addr) {
        return addr & (PAGE_SIZE - 1);
    }
    
    bool is_page_valid(const CachedPage& page) const {
        if (!page.valid) return false;
        
        auto now = std::chrono::steady_clock::now();
        return (now - page.timestamp) < CACHE_TTL_MS;
    }
    
    void update_lru(uintptr_t page_addr) {
        // Move to front of LRU list
        auto it = std::find(lru_order.begin(), lru_order.end(), page_addr);
        if (it != lru_order.end()) {
            lru_order.erase(it);
        }
        lru_order.insert(lru_order.begin(), page_addr);
    }
    
    void evict_lru_pages() {
        while (pages.size() >= MAX_CACHED_PAGES && !lru_order.empty()) {
            uintptr_t lru_addr = lru_order.back();
            lru_order.pop_back();
            pages.erase(lru_addr);
        }
    }

public:
    // Try to read from cache first, fall back to system call if needed
    int cached_read(pid_t pid, void* remote_addr, size_t size, void* local_buf) {
        if (size == 0) return 0;
        if (size > PAGE_SIZE) {
            // For large reads, don't use cache - just do direct system call
            return direct_system_read(pid, remote_addr, size, local_buf);
        }
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(remote_addr);
        uintptr_t page_addr = page_align(addr);
        size_t offset = page_offset(addr);
        
        // Check if read spans multiple pages
        if (offset + size > PAGE_SIZE) {
            // Spans pages - use direct read for simplicity
            return direct_system_read(pid, remote_addr, size, local_buf);
        }
        
        std::lock_guard<std::mutex> lock(cache_mutex);
        
        // Try to find cached page
        auto it = pages.find(page_addr);
        if (it != pages.end() && is_page_valid(*it->second)) {
            // Cache hit!
            std::memcpy(local_buf, it->second->data.data() + offset, size);
            update_lru(page_addr);
            return 0;  // Success
        }
        
        // Cache miss - need to load page
        evict_lru_pages();
        
        auto page = std::make_unique<CachedPage>(page_addr);
        
        // Read entire page from remote process directly into page buffer
        int result = direct_system_read(pid, reinterpret_cast<void*>(page_addr), 
                                       PAGE_SIZE, page->data.data());
        
        if (result == 0) {
            // Success - mark page as valid
            page->valid = true;
            page->timestamp = std::chrono::steady_clock::now();
            
            // Store in cache FIRST
            pages[page_addr] = std::move(page);
            update_lru(page_addr);
            
            // Now copy requested data directly from the cached page
            // (page is now owned by the cache, so access via pages[page_addr])
            std::memcpy(local_buf, pages[page_addr]->data.data() + offset, size);
            
            return 0;  // Success
        }
        
        // System call failed - return error
        return result;
    }
    
    // Direct system call without caching (for large reads or fallback)
    // Forward declaration - implementation in page_cache_integration.cc
    int direct_system_read(pid_t pid, void* remote_addr, size_t size, void* local_buf);
    
    // Clear all cached pages (useful for testing or when process state changes significantly)
    void clear_cache() {
        std::lock_guard<std::mutex> lock(cache_mutex);
        pages.clear();
        lru_order.clear();
    }
    
    // Get cache statistics for debugging/monitoring
    struct CacheStats {
        size_t total_pages;
        size_t valid_pages;
        size_t invalid_pages;
        double avg_age_ms;
    };
    
    CacheStats get_stats() const {
        std::lock_guard<std::mutex> lock(cache_mutex);
        CacheStats stats = {};
        stats.total_pages = pages.size();
        
        auto now = std::chrono::steady_clock::now();
        double total_age_ms = 0.0;
        
        for (const auto& [addr, page] : pages) {
            if (is_page_valid(*page)) {
                stats.valid_pages++;
            } else {
                stats.invalid_pages++;
            }
            
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - page->timestamp).count();
            total_age_ms += age;
        }
        
        if (stats.total_pages > 0) {
            stats.avg_age_ms = total_age_ms / stats.total_pages;
        }
        
        return stats;
    }
};

// Global page cache instance
inline PageMemoryCache& get_page_cache() {
    static PageMemoryCache instance;
    return instance;
}
