#include <Interpreters/RowRefs.h>

#include <Common/typeid_cast.h>
#include <Common/ColumnsHashing.h>
#include <Core/Block.h>
#include <Columns/IColumn.h>


namespace DB
{

namespace
{

/// maps enum values to types
template <typename F>
void callWithType(AsofRowRefs::Type which, F && f)
{
    switch (which)
    {
        case AsofRowRefs::Type::key32:  return f(UInt32());
        case AsofRowRefs::Type::key64:  return f(UInt64());
        case AsofRowRefs::Type::keyf32: return f(Float32());
        case AsofRowRefs::Type::keyf64: return f(Float64());
    }

    __builtin_unreachable();
}

}


AsofRowRefs::AsofRowRefs(Type type)
{
    auto call = [&](const auto & t)
    {
      using T = std::decay_t<decltype(t)>;
      using LookupType = typename Entry<T>::LookupType;
      lookups = std::make_unique<LookupType>();
    };

    callWithType(type, call);
}

void AsofRowRefs::insert(Type type, const IColumn * asof_column, const Block * block, size_t row_num)
{
    auto call = [&](const auto & t)
    {
        using T = std::decay_t<decltype(t)>;
        using LookupPtr = typename Entry<T>::LookupPtr;

        auto * column = typeid_cast<const ColumnVector<T> *>(asof_column);
        T key = column->getElement(row_num);
        auto entry = Entry<T>(key, RowRef(block, row_num));
        std::get<LookupPtr>(lookups)->insert(entry);
    };

    callWithType(type, call);
}

const RowRef * AsofRowRefs::findAsof(Type type, ASOF::Inequality inequality, const IColumn * asof_column, size_t row_num) const
{
    const RowRef * out = nullptr;

    bool ascending = (inequality == ASOF::Inequality::Less) || (inequality == ASOF::Inequality::LessOrEquals);
    bool is_strict = (inequality == ASOF::Inequality::Less) || (inequality == ASOF::Inequality::Greater);

    auto call = [&](const auto & t)
    {
        using T = std::decay_t<decltype(t)>;
        using EntryType = Entry<T>;
        using LookupPtr = typename EntryType::LookupPtr;

        auto * column = typeid_cast<const ColumnVector<T> *>(asof_column);
        T key = column->getElement(row_num);
        auto & typed_lookup = std::get<LookupPtr>(lookups);

        if (is_strict)
            out = typed_lookup->upperBound(EntryType(key), ascending);
        else
            out = typed_lookup->lowerBound(EntryType(key), ascending);
    };

    callWithType(type, call);
    return out;
}

std::optional<AsofRowRefs::Type> AsofRowRefs::getTypeSize(const IColumn * asof_column, size_t & size)
{
    if (typeid_cast<const ColumnVector<UInt32> *>(asof_column))
    {
        size = sizeof(UInt32);
        return Type::key32;
    }
    else if (typeid_cast<const ColumnVector<UInt64> *>(asof_column))
    {
        size = sizeof(UInt64);
        return Type::key64;
    }
    else if (typeid_cast<const ColumnVector<Float32> *>(asof_column))
    {
        size = sizeof(Float32);
        return Type::keyf32;
    }
    else if (typeid_cast<const ColumnVector<Float64> *>(asof_column))
    {
        size = sizeof(Float64);
        return Type::keyf64;
    }

    size = 0;
    return {};
}

}
