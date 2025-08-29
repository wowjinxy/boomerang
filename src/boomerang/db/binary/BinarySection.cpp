#pragma region License
/*
 * This file is part of the Boomerang Decompiler.
 *
 * See the file "LICENSE.TERMS" for information on usage and
 * redistribution of this file, and for a DISCLAIMER OF ALL
 * WARRANTIES.
 */
#pragma endregion License
#include "BinarySection.h"

#include "boomerang/util/IntervalMap.h"
#include "boomerang/util/IntervalSet.h"
#include "boomerang/util/Util.h"
#include "boomerang/util/log/Log.h"

#include <algorithm>
#include <cstring>


class BinarySectionImpl
{
public:
    void addDefinedArea(Address from, Address to) { m_hasDefinedValue.insert(from, to); }

    bool isAddressBss(Address a) const
    {
        if (m_hasDefinedValue.isEmpty()) {
            return true;
        }
        return !m_hasDefinedValue.isContained(a);
    }

    void setAttributeForRange(const QString &name, Address from, Address to)
    {
        m_attributeMap[name].insert(from, to);
    }

    bool addressHasAttribute(const QString &attrib, Address addr) const
    {
        auto it = m_attributeMap.find(attrib);

        if (it == m_attributeMap.end()) {
            return false;
        }

        return it->second.isContained(addr);
    }

public:
    IntervalSet<Address> m_hasDefinedValue;
    std::map<QString, IntervalSet<Address>> m_attributeMap;
};


BinarySection::BinarySection(Address sourceAddr, uint64 size, const QString &name)
    : m_impl(new BinarySectionImpl)
    , m_sectionName(name)
    , m_nativeAddr(sourceAddr)
    , m_hostAddr(HostAddress::INVALID)
    , m_size(size)
    , m_sectionEntrySize(0)
    , m_code(false)
    , m_data(false)
    , m_bss(0)
    , m_readOnly(0)
{
}


BinarySection::~BinarySection()
{
    delete m_impl;
}


bool BinarySection::isAddressBss(Address a) const
{
    if (!Util::inRange(a, m_nativeAddr, m_nativeAddr + m_size)) {
        return false;
    }
    else if (m_bss) {
        return true;
    }
    else if (m_readOnly) {
        return false;
    }

    return m_impl->isAddressBss(a);
}


bool BinarySection::anyDefinedValues() const
{
    return !m_impl->m_hasDefinedValue.isEmpty();
}


void BinarySection::resize(uint32_t sz)
{
    if (sz == m_size) {
        return; // nothing to do
    }

    const Address newEnd = m_nativeAddr + sz;
    if (sz > 0 && newEnd < m_nativeAddr) {
        LOG_ERROR("Cannot resize section '%1' to size %2: address overflow", m_sectionName, sz);
        return;
    }

    // relocating existing host data when expanding
    if (m_hostAddr != HostAddress::INVALID && sz > m_size) {
        Byte *newData = new (std::nothrow) Byte[sz];
        if (newData == nullptr) {
            LOG_ERROR("Cannot resize section '%1' to size %2: allocation failed", m_sectionName,
                      sz);
            return;
        }

        std::memcpy(newData, reinterpret_cast<const void *>(m_hostAddr.value()), m_size);
        m_hostAddr = HostAddress(newData);
    }

    if (sz < m_size) {
        // Truncate defined value ranges and attributes beyond the new end
        IntervalSet<Address> newDefined;
        for (const auto &ival : m_impl->m_hasDefinedValue) {
            Address low  = ival.lower();
            Address high = ival.upper();
            if (low >= newEnd) {
                continue;
            }
            if (high > newEnd) {
                high = newEnd;
            }
            newDefined.insert(low, high);
        }
        m_impl->m_hasDefinedValue = std::move(newDefined);

        for (auto &attrPair : m_impl->m_attributeMap) {
            IntervalSet<Address> newSet;
            for (const auto &ival : attrPair.second) {
                Address low  = ival.lower();
                Address high = ival.upper();
                if (low >= newEnd) {
                    continue;
                }
                if (high > newEnd) {
                    high = newEnd;
                }
                newSet.insert(low, high);
            }
            attrPair.second = std::move(newSet);
        }
    }

    m_size = sz;
}


void BinarySection::addDefinedArea(Address from, Address to)
{
    m_impl->addDefinedArea(from, to);
}


void BinarySection::setAttributeForRange(const QString &name, Address from, Address to)
{
    m_impl->setAttributeForRange(name, from, to);
}


bool BinarySection::addressHasAttribute(const QString &attrName, Address addr) const
{
    return m_impl->addressHasAttribute(attrName, addr);
}
