/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2016 SciDB, Inc.
* All Rights Reserved.
*
* accelerated_io_tools is a plugin for SciDB, an Open Source Array DBMS maintained
* by Paradigm4. See http://www.paradigm4.com/
*
* accelerated_io_tools is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* accelerated_io_tools is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with accelerated_io_tools.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include <query/LogicalOperator.h>
#include "ParseSettings.h"

namespace scidb
{

class LogicalParse : public  LogicalOperator
{
public:
    LogicalParse(const std::string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
    }

    static PlistSpec const* makePlistSpec()
    {
        static PlistSpec argSpec {
            { "", // positionals
              RE(RE::LIST, {
                 RE(PP(PLACEHOLDER_INPUT)),
                 RE(RE::STAR, {
                    RE(PP(PLACEHOLDER_CONSTANT, TID_STRING))
                 })
              })
            }
        };
        return &argSpec;
    }

    ArrayDesc inferSchema(std::vector< ArrayDesc> schemas, shared_ptr< Query> query)
    {
        ArrayDesc const& inputSchema = schemas[0];
        Attributes inputAttributes = inputSchema.getAttributes(true);
        if (inputAttributes.size() != 1 ||
            inputAttributes.firstDataAttribute().getType() != TID_STRING ||
            inputAttributes.firstDataAttribute().getFlags() != 0)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "input to parse must have a single, non-nullable string attribute";
        }
        if (inputSchema.getDimensions().size() != 2 ||
            inputSchema.getDimensions()[0].getStartMin() != 0 ||
            inputSchema.getDimensions()[0].getChunkInterval() != 1 ||
            inputSchema.getDimensions()[1].getStartMin() != 0 ||
            inputSchema.getDimensions()[1].getChunkInterval() != 1)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "input to parse must does not have the correct dimensions (2D, chunk size 1 each)";
        }
        ParseSettings settings (_parameters, true, query);
        size_t numRequestedAttributes = settings.getNumAttributes();
        size_t requestedChunkSize = settings.getChunkSize();
        vector<DimensionDesc> dimensions(3);

        dimensions[0] = DimensionDesc("source_instance_id", 0, 0, CoordinateBounds::getMax(), CoordinateBounds::getMax(), 1, 0);
        dimensions[1] = DimensionDesc("chunk_no",    0, 0, CoordinateBounds::getMax(), CoordinateBounds::getMax(), 1, 0);
        dimensions[2] = DimensionDesc("line_no",     0, 0, CoordinateBounds::getMax(), CoordinateBounds::getMax(), requestedChunkSize, 0);

        Attributes attributes;
        if (settings.getSplitOnDimension())
        {   //add 1 for the error column
            dimensions.push_back(DimensionDesc("attribute_no", 0, 0, numRequestedAttributes, numRequestedAttributes, numRequestedAttributes+1, 0));
            attributes.push_back(AttributeDesc("a", TID_STRING, AttributeDesc::IS_NULLABLE, CompressorType::NONE));
        }
        else
        {
            for(size_t i=0, n=numRequestedAttributes; i<n; ++i)
            {
                ostringstream attname;
                attname<<"a";
                attname<<i;
                attributes.push_back(AttributeDesc(attname.str(),  TID_STRING, AttributeDesc::IS_NULLABLE, CompressorType::NONE));
            }
            attributes.push_back(AttributeDesc("error", TID_STRING, AttributeDesc::IS_NULLABLE, CompressorType::NONE));
        }
        attributes.addEmptyTagAttribute();

        return ArrayDesc("parse", attributes, dimensions, createDistribution(dtUndefined), query->getDefaultArrayResidency());
    }
};

REGISTER_LOGICAL_OPERATOR_FACTORY(LogicalParse, "parse");

} // emd namespace scidb
