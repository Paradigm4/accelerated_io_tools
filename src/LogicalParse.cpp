/*
**
* BEGIN_COPYRIGHT
*
* load_tools is a plugin for SciDB.  Copyright (C) 2008-2014 SciDB, Inc.
*
* load_tools is free software: you can redistribute it and/or modify
* it under the terms of the AFFERO GNU General Public License as published by
* the Free Software Foundation.
*
* load_tools is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
* INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
* NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
* the AFFERO GNU General Public License for the complete license terms.
*
* You should have received a copy of the AFFERO GNU General Public License
* along with load_tools.  If not, see <http://www.gnu.org/licenses/agpl-3.0.html>
*
* END_COPYRIGHT
*/

#include <query/Operator.h>
#include "ParseSettings.h"

namespace scidb
{

class LogicalParse : public  LogicalOperator
{
public:
    LogicalParse(const std::string& logicalName, const std::string& alias):
        LogicalOperator(logicalName, alias)
    {
        ADD_PARAM_INPUT();
        ADD_PARAM_VARIES();
    }

    std::vector<boost::shared_ptr<OperatorParamPlaceholder> > nextVaryParamPlaceholder(const std::vector< ArrayDesc> &schemas)
    {
        std::vector<boost::shared_ptr<OperatorParamPlaceholder> > res;
        res.push_back(END_OF_VARIES_PARAMS());
        if (_parameters.size() < ParseSettings::MAX_PARAMETERS)
        {
            res.push_back(PARAM_CONSTANT("string"));
        }
        return res;
    }

    ArrayDesc inferSchema(std::vector< ArrayDesc> schemas, boost::shared_ptr< Query> query)
    {
        ArrayDesc const& inputSchema = schemas[0];
        Attributes inputAttributes = inputSchema.getAttributes(true);
        if (inputAttributes.size() != 1 ||
            inputAttributes[0].getType() != TID_STRING ||
            inputAttributes[0].getFlags() != 0)
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
        vector<AttributeDesc> attributes(numRequestedAttributes + 1); //+ "error" attribute
        for(size_t i=0, n=numRequestedAttributes; i<n; ++i)
        {
            ostringstream attname;
            attname<<"a";
            attname<<i;
            attributes[i] =  AttributeDesc((AttributeID)i, attname.str(),  TID_STRING, 0, 0);
        }
        attributes[numRequestedAttributes] = AttributeDesc((AttributeID)numRequestedAttributes, "error", TID_STRING, AttributeDesc::IS_NULLABLE, 0);
        attributes = addEmptyTagAttribute(attributes);
        size_t requestedChunkSize = settings.getChunkSize();
        vector<DimensionDesc> dimensions(3);
        dimensions[0] = DimensionDesc("source_instance_id", 0, 0, MAX_COORDINATE, MAX_COORDINATE, 1, 0);
        dimensions[1] = DimensionDesc("chunk_no",    0, 0, MAX_COORDINATE, MAX_COORDINATE, 1, 0);
        dimensions[2] = DimensionDesc("line_no",     0, 0, MAX_COORDINATE, MAX_COORDINATE, requestedChunkSize, 0);
        return ArrayDesc("parse", attributes, dimensions);
    }

};

REGISTER_LOGICAL_OPERATOR_FACTORY(LogicalParse, "parse");

} // emd namespace scidb