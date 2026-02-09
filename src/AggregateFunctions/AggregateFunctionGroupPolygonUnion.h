#pragma once

#include <AggregateFunctions/IAggregateFunction.h>

#include <Columns/ColumnArray.h>
#include <Columns/ColumnTuple.h>
#include <Columns/ColumnsNumber.h>

#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeFactory.h>

#include <IO/ReadHelpers.h>
#include <IO/WriteHelpers.h>

#include <Functions/geometryConverters.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

namespace DB
{

template <typename Point>
struct AggregateFunctionGroupPolygonUnionData
{
    MultiPolygon<Point> accumulated_union;
    bool has_value = false;
};


template <typename Point>
class AggregateFunctionGroupPolygonUnion final
    : public IAggregateFunctionDataHelper<AggregateFunctionGroupPolygonUnionData<Point>, AggregateFunctionGroupPolygonUnion<Point>>
{
private:
    using Data = AggregateFunctionGroupPolygonUnionData<Point>;

public:
    explicit AggregateFunctionGroupPolygonUnion(const DataTypes & argument_types_)
        : IAggregateFunctionDataHelper<Data, AggregateFunctionGroupPolygonUnion<Point>>(
            argument_types_, {}, DataTypeFactory::instance().get("MultiPolygon"))
    {}

    String getName() const override { return "groupPolygonUnion"; }

    bool allocatesMemoryInArena() const override { return false; }

    void add(AggregateDataPtr __restrict place, const IColumn ** columns, size_t row_num, Arena *) const override
    {
        auto & state = this->data(place);

        // Convert the column data to a MultiPolygon for this row
        auto multi_polygons = ColumnToMultiPolygonsConverter<Point>::convert(columns[0]->getPtr());

        if (row_num >= multi_polygons.size())
            return;

        const auto & current_polygon = multi_polygons[row_num];

        if (!state.has_value)
        {
            state.accumulated_union = current_polygon;
            state.has_value = true;
        }
        else
        {
            MultiPolygon<Point> new_union;
            boost::geometry::union_(state.accumulated_union, current_polygon, new_union);
            state.accumulated_union = std::move(new_union);
        }
    }

    void merge(AggregateDataPtr __restrict place, ConstAggregateDataPtr rhs, Arena *) const override
    {
        auto & state = this->data(place);
        const auto & rhs_state = this->data(rhs);

        if (!rhs_state.has_value)
            return;

        if (!state.has_value)
        {
            state.accumulated_union = rhs_state.accumulated_union;
            state.has_value = true;
        }
        else
        {
            MultiPolygon<Point> new_union;
            boost::geometry::union_(state.accumulated_union, rhs_state.accumulated_union, new_union);
            state.accumulated_union = std::move(new_union);
        }
    }

    void serialize(ConstAggregateDataPtr __restrict place, WriteBuffer & buf, std::optional<size_t> /* version */) const override
    {
        const auto & state = this->data(place);

        writeBinaryLittleEndian(state.has_value, buf);
        if (!state.has_value)
            return;

        // Write number of polygons
        writeVarUInt(state.accumulated_union.size(), buf);

        for (const auto & polygon : state.accumulated_union)
        {
            // Write outer ring
            const auto & outer = polygon.outer();
            writeVarUInt(outer.size(), buf);
            for (const auto & point : outer)
            {
                writeBinaryLittleEndian(point.template get<0>(), buf);
                writeBinaryLittleEndian(point.template get<1>(), buf);
            }

            // Write inner rings (holes)
            writeVarUInt(polygon.inners().size(), buf);
            for (const auto & inner : polygon.inners())
            {
                writeVarUInt(inner.size(), buf);
                for (const auto & point : inner)
                {
                    writeBinaryLittleEndian(point.template get<0>(), buf);
                    writeBinaryLittleEndian(point.template get<1>(), buf);
                }
            }
        }
    }

    void deserialize(AggregateDataPtr __restrict place, ReadBuffer & buf, std::optional<size_t> /* version */, Arena *) const override
    {
        auto & state = this->data(place);

        readBinaryLittleEndian(state.has_value, buf);
        if (!state.has_value)
            return;

        size_t num_polygons;
        readVarUInt(num_polygons, buf);

        state.accumulated_union.resize(num_polygons);

        for (size_t i = 0; i < num_polygons; ++i)
        {
            // Read outer ring
            size_t outer_size;
            readVarUInt(outer_size, buf);
            state.accumulated_union[i].outer().resize(outer_size);
            for (size_t j = 0; j < outer_size; ++j)
            {
                Float64 x, y;
                readBinaryLittleEndian(x, buf);
                readBinaryLittleEndian(y, buf);
                state.accumulated_union[i].outer()[j] = Point(x, y);
            }

            // Read inner rings
            size_t num_inners;
            readVarUInt(num_inners, buf);
            state.accumulated_union[i].inners().resize(num_inners);
            for (size_t k = 0; k < num_inners; ++k)
            {
                size_t inner_size;
                readVarUInt(inner_size, buf);
                state.accumulated_union[i].inners()[k].resize(inner_size);
                for (size_t j = 0; j < inner_size; ++j)
                {
                    Float64 x, y;
                    readBinaryLittleEndian(x, buf);
                    readBinaryLittleEndian(y, buf);
                    state.accumulated_union[i].inners()[k][j] = Point(x, y);
                }
            }
        }
    }

    void insertResultInto(AggregateDataPtr __restrict place, IColumn & to, Arena *) const override
    {
        auto & state = this->data(place);

        auto & column_array = assert_cast<ColumnArray &>(to);

        if (!state.has_value)
        {
            // Insert empty multi-polygon
            column_array.getOffsets().push_back(column_array.getOffsets().back());
            return;
        }

        // Use MultiPolygonSerializer to add the result
        MultiPolygonSerializer<Point> serializer;
        serializer.add(state.accumulated_union);
        auto result_column = serializer.finalize();

        // Copy from result to output
        column_array.insertFrom(*result_column, 0);
    }
};

}
