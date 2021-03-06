/*
**
* BEGIN_COPYRIGHT
*
* Copyright (C) 2008-2020 Paradigm4 Inc.
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

#include "AioSaveSettings.h"

namespace scidb
{

class LogicalAioSave : public  LogicalOperator
{
public:
    LogicalAioSave(const std::string& logicalName, const std::string& alias):
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
            },
            { KW_PATHS, RE(RE::OR, {
                           RE(PP(PLACEHOLDER_EXPRESSION, TID_STRING)),
                           RE(RE::GROUP, {
                              RE(PP(PLACEHOLDER_EXPRESSION, TID_STRING)),
                              RE(RE::PLUS, {
                                 RE(PP(PLACEHOLDER_EXPRESSION, TID_STRING))
                              })
                           })
                        })
            },
            { KW_INSTANCES, RE(RE::OR, {
                            RE(PP(PLACEHOLDER_EXPRESSION, TID_INT64)),
                            RE(RE::GROUP, {
                                   RE(PP(PLACEHOLDER_EXPRESSION, TID_INT64)),
                                   RE(RE::PLUS, {
                                      RE(PP(PLACEHOLDER_EXPRESSION, TID_INT64))
                                   })
                              })
                           })
            },
            { KW_BUF_SZ, RE(PP(PLACEHOLDER_CONSTANT, TID_INT64)) },
            { KW_LINE_DELIM, RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)) },
            { KW_ATTR_DELIM, RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)) },
            { KW_CELLS_PER_CHUNK, RE(PP(PLACEHOLDER_CONSTANT, TID_INT64)) },
            { KW_FORMAT, RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)) },
            { KW_NULL_PATTERN, RE(PP(PLACEHOLDER_CONSTANT, TID_STRING)) },
            { KW_PRECISION, RE(PP(PLACEHOLDER_CONSTANT, TID_INT32)) },
            { KW_ATTS_ONLY, RE(PP(PLACEHOLDER_CONSTANT, TID_BOOL)) },
            { KW_RESULT_LIMIT, RE(PP(PLACEHOLDER_CONSTANT, TID_INT64)) }
        };
        return &argSpec;
    }

    ArrayDesc inferSchema(std::vector< ArrayDesc> schemas, shared_ptr< Query> query)
    {
        ArrayDesc const& inputSchema = schemas[0];
        AioSaveSettings settings (_parameters, _kwParameters, true, query);
        vector<DimensionDesc> dimensions(3);
        size_t const nInstances = query->getInstancesCount();
        dimensions[0] = DimensionDesc("chunk_no",    0, 0, CoordinateBounds::getMax(), CoordinateBounds::getMax(), 1, 0);
        dimensions[1] = DimensionDesc("dest_instance_id",   0, 0, nInstances-1, nInstances-1, 1, 0);
        dimensions[2] = DimensionDesc("source_instance_id", 0, 0, nInstances-1, nInstances-1, 1, 0);
        Attributes attributes;
        attributes.push_back(AttributeDesc("val", TID_STRING, AttributeDesc::IS_NULLABLE, CompressorType::NONE));
        attributes.addEmptyTagAttribute();
        return ArrayDesc("aio_save", attributes, dimensions, createDistribution(defaultDistType()), query->getDefaultArrayResidency(), 0, false);
    }
};

REGISTER_LOGICAL_OPERATOR_FACTORY(LogicalAioSave, "aio_save");

} // emd namespace scidb
