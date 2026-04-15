/******************************************************************************
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Michael Coutlakis
 *****************************************************************************/
#pragma once
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

    bool add(T t)
    {
        if(find_ptr(get_reg_key(t)))
            return false;
        m_items.push_back(std::move(t));
    }

    T *find_ptr(key_type key)
    {
        auto it = std::find_if(
            m_items.begin(),
            m_items.end(),
            [&](const auto &item) { return get_reg_key(item) == key; });

        return it != m_items.end() ? &*it : nullptr;
    }

    const T *find_ptr(key_type key) const
    {
        auto it = std::find_if(
            m_items.begin(),
            m_items.end(),
            [&](const auto &item) { return get_reg_key(item) == key; });

        return it != m_items.end() ? &*it : nullptr;
    }

protected:
    auto &items() noexcept { return m_items; }
    const auto &items() const noexcept { return m_items; }

private:
    container_type m_items;
};