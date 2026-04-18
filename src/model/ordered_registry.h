/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
#include <cstddef>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

template <typename T, typename RegKey, RegKey (*get_reg_key)(const T &)>
class ordered_registry
{
public:
    using container_type = std::vector<T>;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;
    using key_type = RegKey;

    auto begin() noexcept { return m_items.begin(); }
    auto end() noexcept { return m_items.end(); }

    auto begin() const noexcept { return m_items.begin(); }
    auto end() const noexcept { return m_items.end(); }

    auto size() const noexcept { return m_items.size(); }

    void clear() { m_items.clear(); }
    void erase(key_type key)
    {
        if(auto p = find_ptr(key); p)
            m_items.erase(m_items.begin() + std::distance(m_items.data(), p));
    }
    void erase(T *ptr)
    {
        auto offset = std::distance(m_items.data(), ptr);
        if(ptr < m_items.data() || offset >= m_items.size())
            throw std::out_of_range("Cannot erase item not in container");
        m_items.erase(m_items.begin() + offset);
    }
    bool add(T t)
    {
        if(find_ptr(get_reg_key(t)))
            return false;
        m_items.push_back(std::move(t));
        return true;
    }

    T *find_ptr(key_type key)
    {
        size_t k = find_idx(key);
        return k != npos ? m_items.data() + k : nullptr;
    }

    const T *find_ptr(key_type key) const
    {
        size_t k = find_idx(key);
        return k != npos ? m_items.data() + k : nullptr;
    }

    iterator find(key_type key)
    {
        size_t k = find_idx(key);
        return k != npos ? m_items.begin() + k : end();
    }

protected:
    auto &items() noexcept { return m_items; }
    const auto &items() const noexcept { return m_items; }

    size_t find_idx(key_type key) const
    {
        for(size_t u = 0U; u != m_items.size(); ++u)
            if(get_reg_key(m_items[u]) == key)
                return u;
        return npos;
    }

private:
    container_type m_items;
    static constexpr auto npos{std::numeric_limits<size_t>::max()};
};