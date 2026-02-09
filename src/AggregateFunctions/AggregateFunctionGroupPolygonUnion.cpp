#include <memory>
#include <AggregateFunctions/AggregateFunctionFactory.h>
#include <AggregateFunctions/AggregateFunctionGroupPolygonUnion.h>
#include <AggregateFunctions/FactoryHelpers.h>

namespace DB
{

struct Settings;

namespace ErrorCodes
{
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
}

namespace
{

AggregateFunctionPtr createAggregateFunctionGroupPolygonUnion(
    const std::string & name, const DataTypes & argument_types, const Array & parameters, const Settings *)
{
    assertNoParameters(name, parameters);
    assertUnary(name, argument_types);

    // MultiPolygon is internally Array(Array(Array(Tuple(Float64, Float64))))
    // Accept either the named type or the array representation
    const auto & arg_type = argument_types[0];
    const auto type_name = arg_type->getName();
    
    // Check if it's a valid geometry type (MultiPolygon or equivalent array structure)
    bool is_valid = (type_name == "MultiPolygon") || 
                    (type_name.starts_with("Array(Array(Array(Tuple("));

    if (!is_valid)
    {
        throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
            "Illegal type {} of argument for aggregate function {}, expected MultiPolygon or Array(Array(Array(Tuple(Float64, Float64))))",
            type_name, name);
    }

    return std::make_shared<AggregateFunctionGroupPolygonUnion<CartesianPoint>>(argument_types);
}

}

void registerAggregateFunctionGroupPolygonUnion(AggregateFunctionFactory & factory)
{
    FunctionDocumentation::Description description = R"(
Calculates the union of all polygons in a group. Takes MultiPolygon values and returns a single MultiPolygon
representing the union of all input polygons.
    )";
    FunctionDocumentation::Syntax syntax = "groupPolygonUnion(multipolygon)";
    FunctionDocumentation::Arguments arguments = {
        {"multipolygon", "A column of MultiPolygon values.", {"MultiPolygon"}}
    };
    FunctionDocumentation::ReturnedValue returned_value = {
        "Returns a MultiPolygon representing the union of all input polygons.", {"MultiPolygon"}
    };
    FunctionDocumentation::Examples examples = {
        {
            "Basic usage",
            R"(
SELECT wkt(groupPolygonUnion(polygon)) FROM shapes GROUP BY category;
            )",
            R"(
MULTIPOLYGON(((0 0,0 10,10 10,10 0,0 0)))
            )"
        }
    };
    FunctionDocumentation::IntroducedIn introduced_in = {24, 1};
    FunctionDocumentation::Category category = FunctionDocumentation::Category::AggregateFunction;
    FunctionDocumentation documentation = {description, syntax, arguments, {}, returned_value, examples, introduced_in, category};

    AggregateFunctionProperties properties = {.returns_default_when_only_null = true, .is_order_dependent = false};

    factory.registerFunction("groupPolygonUnion", {createAggregateFunctionGroupPolygonUnion, properties, documentation});
}

}
