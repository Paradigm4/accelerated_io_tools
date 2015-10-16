/*
**
* BEGIN_COPYRIGHT
*
* PARADIGM4 INC.
* This file is part of the Paradigm4 Enterprise SciDB distribution kit
* and may only be used with a valid Paradigm4 contract and in accord
* with the terms and conditions specified by that contract.
*
* Copyright (C) 2010 - 2015 Paradigm4 Inc.
* All Rights Reserved.
*
* END_COPYRIGHT
*/


#include <limits>
#include <sstream>
#include <memory>
#include <string>
#include <vector>
#include <ctype.h>

#include <system/Exceptions.h>
#include <system/SystemCatalog.h>
#include <system/Sysinfo.h>

#include <query/TypeSystem.h>
#include <query/FunctionDescription.h>
#include <query/FunctionLibrary.h>
#include <query/Operator.h>
#include <query/TypeSystem.h>
#include <query/FunctionLibrary.h>
#include <query/Operator.h>
#include <array/DBArray.h>
#include <array/Tile.h>
#include <array/TileIteratorAdaptors.h>
#include <util/Platform.h>

#include "UnparseTemplateParser.h"

#include <boost/algorithm/string.hpp>
#include <boost/unordered_map.hpp>

#include "AioSaveSettings.h"

#ifdef CPP11
using std::make_shared;
#else
using boost::make_shared;
#endif

using std::make_shared;
using boost::algorithm::is_from_range;

namespace scidb
{

static log4cxx::LoggerPtr logger(log4cxx::Logger::getLogger("scidb.alt_save"));

using namespace scidb;

static void EXCEPTION_ASSERT(bool cond)
{
    if (! cond)
    {
        throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Internal inconsistency";
    }
}

class MemChunkBuilder
{
private:
    size_t      _allocSize;
    char*       _chunkStartPointer;
    char*       _dataStartPointer;
    char*       _writePointer;
    uint32_t*   _sizePointer;
    uint64_t*   _dataSizePointer;
    MemChunk    _chunk;

public:
    static size_t chunkDataOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 5);
    }

    static size_t chunkSizeOffset()
    {
        return (sizeof(ConstRLEPayload::Header) + 2 * sizeof(ConstRLEPayload::Segment) + sizeof(varpart_offset_t) + 1);
    }

    static const size_t s_startingSize = 20*1024*1024;

    MemChunkBuilder():
        _allocSize(s_startingSize)
    {
        _chunk.allocate(_allocSize);
        _chunkStartPointer = (char*) _chunk.getData();
        ConstRLEPayload::Header* hdr = (ConstRLEPayload::Header*) _chunkStartPointer;
        hdr->_magic = RLE_PAYLOAD_MAGIC;
        hdr->_nSegs = 1;
        hdr->_elemSize = 0;
        hdr->_dataSize = 0;
        _dataSizePointer = &(hdr->_dataSize);
        hdr->_varOffs = sizeof(varpart_offset_t);
        hdr->_isBoolean = 0;
        ConstRLEPayload::Segment* seg = (ConstRLEPayload::Segment*) (hdr+1);
        *seg =  ConstRLEPayload::Segment(0,0,false,false);
        ++seg;
        *seg =  ConstRLEPayload::Segment(1,0,false,false);
        varpart_offset_t* vp =  (varpart_offset_t*) (seg+1);
        *vp = 0;
        uint8_t* sizeFlag = (uint8_t*) (vp+1);
        *sizeFlag =0;
        _sizePointer = (uint32_t*) (sizeFlag + 1);
        _dataStartPointer = (char*) (_sizePointer+1);
        _writePointer = _dataStartPointer;
    }

    ~MemChunkBuilder()
    {}

    inline size_t getTotalSize() const
    {
        return (_writePointer - _chunkStartPointer);
    }

    inline void addData(char const* data, size_t const size)
    {
        if( getTotalSize() + size > _allocSize)
        {
            size_t const mySize = getTotalSize();
            while (mySize + size > _allocSize)
            {
                _allocSize = _allocSize * 2;
            }
            _chunk.allocate(_allocSize);
            _chunkStartPointer = (char*) _chunk.getData();
            _dataStartPointer = _chunkStartPointer + chunkDataOffset();
            _sizePointer = (uint32_t*) (_chunkStartPointer + chunkSizeOffset());
            _writePointer = _chunkStartPointer + mySize;
            ConstRLEPayload::Header* hdr = (ConstRLEPayload::Header*) _chunkStartPointer;
            _dataSizePointer = &(hdr->_dataSize);
        }
        memcpy(_writePointer, data, size);
        _writePointer += size;
    }

    inline MemChunk& getChunk()
    {
        *_sizePointer = (_writePointer - _dataStartPointer);
        *_dataSizePointer = (_writePointer - _dataStartPointer) + 5 + sizeof(varpart_offset_t);
        return _chunk;
    }

    inline void reset()
    {
        _writePointer = _dataStartPointer;
    }
};

class ArrayCursor
{
private:
    shared_ptr<Array> _input;
    size_t const _nAttrs;
    vector <Value const *> _currentCell;
    bool _end;
    vector<shared_ptr<ConstArrayIterator> > _inputArrayIters;
    vector<shared_ptr<ConstChunkIterator> > _inputChunkIters;

public:
    ArrayCursor (shared_ptr<Array> const& input):
        _input(input),
        _nAttrs(input->getArrayDesc().getAttributes(true).size()),
        _currentCell(_nAttrs, 0),
        _end(false),
        _inputArrayIters(_nAttrs, 0),
        _inputChunkIters(_nAttrs, 0)
    {
        for(size_t i =0; i<_nAttrs; ++i)
        {
            _inputArrayIters[i] = _input->getConstIterator(i);
        }
        if (_inputArrayIters[0]->end())
        {
            _end=true;
        }
        else
        {
            advance();
        }
    }

    bool end() const
    {
        return _end;
    }

    size_t nAttrs() const
    {
        return _nAttrs;
    }

    void advance()
    {
        if(_end)
        {
            throw SYSTEM_EXCEPTION(SCIDB_SE_INTERNAL, SCIDB_LE_ILLEGAL_OPERATION) << "Internal error: iterating past end of cursor";
        }
        if (_inputChunkIters[0] == 0) //1st time!
        {
            for(size_t i =0; i<_nAttrs; ++i)
            {
                _inputChunkIters[i] = _inputArrayIters[i]->getChunk().getConstIterator(ConstChunkIterator::IGNORE_OVERLAPS | ConstChunkIterator::IGNORE_EMPTY_CELLS);
            }
        }
        else if (!_inputChunkIters[0]->end()) //not first time!
        {
            for(size_t i =0; i<_nAttrs; ++i)
            {
                ++(*_inputChunkIters[i]);
            }
        }
        while(_inputChunkIters[0]->end())
        {
            for(size_t i =0; i<_nAttrs; ++i)
            {
                ++(*_inputArrayIters[i]);
            }
            if(_inputArrayIters[0]->end())
            {
                _end = true;
                return;
            }
            for(size_t i =0; i<_nAttrs; ++i)
            {
                _inputChunkIters[i] = _inputArrayIters[i]->getChunk().getConstIterator(ConstChunkIterator::IGNORE_OVERLAPS | ConstChunkIterator::IGNORE_EMPTY_CELLS);
            }
        }
        for(size_t i =0; i<_nAttrs; ++i)
        {
            _currentCell[i] = &(_inputChunkIters[i]->getItem());
        }
    }

    vector <Value const *> const& getCell()
    {
        return _currentCell;
    }
};

class BinaryConvertedArray : public SinglePassArray
{
private:
    typedef SinglePassArray super;
    size_t           _rowIndex;
    Address          _chunkAddress;
    ArrayCursor      _inputCursor;
    MemChunkBuilder  _chunkBuilder;
    weak_ptr<Query>  _query;
    ExchangeTemplate _templ;
    size_t const     _linesPerChunk;
    size_t const     _nAttrs;
    size_t const     _nColumns;
    vector< Value >  _cnvValues;
    vector< char >   _padBuffer;

    static inline size_t skip_bytes(ExchangeTemplate::Column const& c)
    {
        SCIDB_ASSERT(c.skip);
        return (c.fixedSize ? c.fixedSize : sizeof(uint32_t)) + c.nullable;
    }

public:
    BinaryConvertedArray(ArrayDesc const& schema,
                         shared_ptr<Array>& inputArray,
                         shared_ptr<Query>& query,
                         AioSaveSettings const& settings):
        super(schema),
        _rowIndex(0),
        _chunkAddress(0, Coordinates(2,0)),
        _inputCursor(inputArray),
        _query(query),
        _templ(TemplateParser::parse(inputArray->getArrayDesc(), settings.getBinaryFormatString(), false)),
        _linesPerChunk(settings.getLinesPerChunk()),
        _nAttrs(_inputCursor.nAttrs()),
        _nColumns(_templ.columns.size()),
        _cnvValues(_nAttrs),
        _padBuffer(sizeof(uint64_t) + 1, '\0')
    {
        _chunkAddress.coords[1] = query->getInstanceID();
    }

    virtual ~BinaryConvertedArray()
    {}

    size_t getCurrentRowIndex() const
    {
        return _rowIndex;
    }

    bool moveNext(size_t rowIndex)
    {
        if(_inputCursor.end())
        {
            return false;
        }
        _chunkBuilder.reset();
        size_t nCells = 0;
        for (size_t c = 0, i = 0; c < _nColumns; ++c)
        {
            ExchangeTemplate::Column const& column = _templ.columns[c];
            if (column.skip)
            {
                // Prepare to write (enough) padding.
                size_t pad = skip_bytes(column);
                if (pad > _padBuffer.size())
                {
                    _padBuffer.resize(pad);
                }
            }
            else
            {
                if (column.converter)
                {
                    _cnvValues[i] = Value(column.externalType);
                }
                ++i;            // next attribute
            }
        }
        while(nCells < _linesPerChunk && !_inputCursor.end())
        {
            vector <Value const *> cell = _inputCursor.getCell();
            for (size_t c = 0, i = 0; c < _nColumns; ++c)
            {
                ExchangeTemplate::Column const& column = _templ.columns[c];
                if (column.skip)
                {
                    size_t pad = skip_bytes(column);
                    _chunkBuilder.addData(&(_padBuffer[0]), _padBuffer.size());
                }
                else
                {
                    Value const* v = cell[i];
                    if (column.nullable)
                    {
                        if (v->getMissingReason() > 127)
                        {
                            LOG4CXX_WARN(logger, "Missing reason " << v->getMissingReason() << " cannot be stored in binary file");
                        }
                        int8_t missingReason = (int8_t)v->getMissingReason();
                        _chunkBuilder.addData( (char*) (&missingReason), sizeof(missingReason));
                    }
                    if (v->isNull())
                    {
                        if (!column.nullable)
                        {
                            throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_ASSIGNING_NULL_TO_NON_NULLABLE);
                        }
                        // for varying size type write 4-bytes counter
                        size_t size = column.fixedSize ? column.fixedSize : sizeof(uint32_t);
                        _chunkBuilder.addData( &(_padBuffer[0]), size);
                    }
                    else
                    {
                        if (column.converter)
                        {
                            column.converter(&v, &_cnvValues[i], NULL);
                            v = &_cnvValues[i];
                        }
                        if (v->size() > numeric_limits<uint32_t>::max())
                        {
                            throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_TRUNCATION) << v->size() << numeric_limits<uint32_t>::max();
                        }
                        uint32_t size = (uint32_t)v->size();
                        if (column.fixedSize == 0)
                        { // varying size type
                            _chunkBuilder.addData( (char*) (&size), sizeof(size));
                            _chunkBuilder.addData( (char*) v->data(), size);
                        }
                        else
                        {
                            if (size > column.fixedSize)
                            {
                                throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_TRUNCATION) << size << column.fixedSize;
                            }
                            _chunkBuilder.addData( (char*) v->data(), size);
                            if (size < column.fixedSize)
                            {
                                size_t padSize = column.fixedSize - size;
                                assert(padSize <= _padBuffer.size());
                                _chunkBuilder.addData(&(_padBuffer[0]), padSize);
                            }
                        }
                    }
                    ++i;
                }
            }
            _inputCursor.advance();
            ++nCells;
        }
        ++_rowIndex;
        return true;
    }

    ConstChunk const& getChunk(AttributeID attr, size_t rowIndex)
    {
        _chunkAddress.coords[0] = _rowIndex  -1;
        shared_ptr<Query> query = Query::getValidQueryPtr(_query);
        MemChunk& ch = _chunkBuilder.getChunk();
        ch.initialize(this, &super::getArrayDesc(), _chunkAddress, 0);
        return ch;
    }
};

class TextConvertedArray : public SinglePassArray
{
private:
    typedef SinglePassArray super;
    size_t           _rowIndex;
    Address          _chunkAddress;
    ArrayCursor      _inputCursor;
    MemChunkBuilder  _chunkBuilder;
    weak_ptr<Query>  _query;
    size_t const     _linesPerChunk;
    char const       _attDelim;
    char const       _lineDelim;
    vector<bool>            _isString;
    vector<FunctionPointer> _converters;
    Value                   _stringBuf;

public:
    TextConvertedArray(ArrayDesc const& schema,
                       shared_ptr<Array>& inputArray,
                       shared_ptr<Query>& query,
                       AioSaveSettings const& settings):
        super(schema),
        _rowIndex(0),
        _chunkAddress(0, Coordinates(2,0)),
        _inputCursor(inputArray),
        _query(query),
        _linesPerChunk(settings.getLinesPerChunk()),
        _attDelim(settings.getAttributeDelimiter()),
        _lineDelim(settings.getLineDelimiter()),
        _isString(_inputCursor.nAttrs(), false),
        _converters(_inputCursor.nAttrs(), 0)
    {
        Attributes const& inputAttrs = inputArray->getArrayDesc().getAttributes(true);
        for (size_t i = 0; i < _inputCursor.nAttrs(); ++i)
        {
            if(inputAttrs[i].getType() == TID_STRING)
            {
                _isString[i] = true;
            }
            else
            {
                _converters[i] = FunctionLibrary::getInstance()->findConverter(
                    inputAttrs[i].getType(),
                    TID_STRING,
                    false);
            }
        }
        _chunkAddress.coords[1] = query->getInstanceID();
    }

    virtual ~TextConvertedArray()
    {}

    size_t getCurrentRowIndex() const
    {
        return _rowIndex;
    }

    bool moveNext(size_t rowIndex)
    {
        if(_inputCursor.end())
        {
            return false;
        }
        _chunkBuilder.reset();
        size_t nCells = 0;
        ostringstream outputBuf;
        while(nCells < _linesPerChunk && !_inputCursor.end())
        {
            vector <Value const *> cell = _inputCursor.getCell();
            for (size_t i = 0; i < _inputCursor.nAttrs(); ++i)
            {
                Value const* v = cell[i];
                if (i)
                {
                    outputBuf<<_attDelim;
                }
                if(v->isNull())
                {
                    //TODO: print nothing for now (the TSV way). In the future - add a null representation option to settings
                    outputBuf<<"\\N";
                }
                else
                {
                    if(_isString[i])
                    {
                        outputBuf<<v->getString();
                    }
                    else
                    {
                        (*_converters[i])(&v, &_stringBuf, NULL);
                        outputBuf<<_stringBuf.getString();
                    }
                }
            }
            outputBuf<<_lineDelim;
            _inputCursor.advance();
            ++nCells;
        }
        string s = outputBuf.str();
        _chunkBuilder.addData(s.c_str(), s.size());
        ++_rowIndex;
        return true;
    }

    ConstChunk const& getChunk(AttributeID attr, size_t rowIndex)
    {
        _chunkAddress.coords[0] = _rowIndex  -1;
        shared_ptr<Query> query = Query::getValidQueryPtr(_query);
        MemChunk& ch = _chunkBuilder.getChunk();
        ch.initialize(this, &super::getArrayDesc(), _chunkAddress, 0);
        return ch;
    }
};


struct AwIoError
{
    AwIoError(int x) : error(x) {}
    int     error;
};

uint64_t saveToDisk(shared_ptr<Array> const& array,
                    string file,
                    std::shared_ptr<Query> const& query,
                    bool const isBinary,
                    bool const append)
{
    ArrayDesc const& desc = array->getArrayDesc();
    const size_t N_ATTRS = desc.getAttributes(true).size();
    EXCEPTION_ASSERT(N_ATTRS==1);
    FILE* f;
    LOG4CXX_DEBUG(logger, "ALT_SAVE>> opening file")
    if (file == "console" || file == "stdout")
    {
        f = stdout;
    }
    else if (file == "stderr")
    {
        f = stderr;
    }
    else
    {
        f = fopen(file.c_str(), isBinary ? append ? "ab" : "wb" : append ? "a" : "w");
        if (NULL == f)
        {
            int error = errno;
            LOG4CXX_DEBUG(logger, "Attempted to open output file '" << file << "' failed: " << ::strerror(error) << " (" << error);
            throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_CANT_OPEN_FILE) << file << ::strerror(error) << error;
        }
        struct flock flc;
        flc.l_type = F_WRLCK;
        flc.l_whence = SEEK_SET;
        flc.l_start = 0;
        flc.l_len = 1;
        int rc = fcntl(fileno(f), F_SETLK, &flc);
        if (rc == -1) {
            throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_CANT_LOCK_FILE) << file << ::strerror(errno) << errno;
        }
    }
    LOG4CXX_DEBUG(logger, "ALT_SAVE>> starting write")
    size_t bytesWritten = 0;
    try
    {
        shared_ptr<ConstArrayIterator> arrayIter = array->getConstIterator(0);
        for (size_t n = 0; !arrayIter->end(); n++)
        {
            ConstChunk const& ch = arrayIter->getChunk();
            PinBuffer scope(ch);
            uint32_t* sizePointer = (uint32_t*) (((char*)ch.getData()) + MemChunkBuilder::chunkSizeOffset());
            uint32_t size = *sizePointer;
            bytesWritten += size;
            char* data = ((char*)ch.getData() + MemChunkBuilder::chunkDataOffset());
            if (fwrite(data, 1, size, f) != size)
            {
                int err = errno ? errno : EIO;
                throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_FILE_WRITE_ERROR) << ::strerror(err) << err;
            }
            ++(*arrayIter);
        }
    }
    catch (AwIoError& e)
    {
        if (f == stdout || f == stderr)
        {
            ::fflush(f);
        }
        else
        {
            ::fclose(f);
        }
        throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_FILE_WRITE_ERROR) << ::strerror(e.error) << e.error;
    }
    LOG4CXX_DEBUG(logger, "ALT_SAVE>> wrote "<< bytesWritten<< " bytes, closing")
    int rc(0);
    if (f == stdout || f == stderr)
    {
        rc = ::fflush(f);
    }
    else
    {
        rc = ::fclose(f);
    }
    if (rc != 0)
    {
        int err = errno ? errno : EIO;
        throw USER_EXCEPTION(SCIDB_SE_ARRAY_WRITER, SCIDB_LE_FILE_WRITE_ERROR) << ::strerror(err) << err;
    }
    LOG4CXX_DEBUG(logger, "ALT_SAVE>> closed")
    return 0;
}


class PhysicalAioSave : public PhysicalOperator
{
public:
    PhysicalAioSave(std::string const& logicalName,
                  std::string const& physicalName,
                  Parameters const& parameters,
                   ArrayDesc const& schema):
        PhysicalOperator(logicalName, physicalName, parameters, schema)
    {}

    virtual bool changesDistribution(std::vector<ArrayDesc> const&) const
    {
        return true;
    }

#ifdef CPP11
    virtual RedistributeContext getOutputDistribution(std::vector<RedistributeContext> const&, std::vector<ArrayDesc> const&) const
    {
        return RedistributeContext(psUndefined);
    }
#else
    virtual ArrayDistribution getOutputDistribution(std::vector<ArrayDistribution> const&, std::vector<ArrayDesc> const&) const
    {
        return ArrayDistribution(psUndefined);
    }
#endif

    std::shared_ptr< Array> execute(std::vector< std::shared_ptr< Array> >& inputArrays, std::shared_ptr<Query> query)
    {
        AioSaveSettings settings (_parameters, false, query);
        shared_ptr<Array>& input = inputArrays[0];
        shared_ptr< Array> outArray;
        if(settings.isBinaryFormat())
        {
            outArray.reset(new BinaryConvertedArray(_schema, input, query, settings));
        }
        else
        {
            outArray.reset(new TextConvertedArray(_schema, input, query, settings));
        }
        ArrayDesc const& inputArrayDesc = outArray->getArrayDesc();
        shared_ptr<Array> tmpRedistedInput;
        InstanceID const myInstanceID = query->getInstanceID();
        InstanceID const saveInstanceID = settings.getSaveInstanceId();
        const Attributes& attribs = inputArrayDesc.getAttributes();
        LOG4CXX_DEBUG(logger, "ALT_SAVE>> Starting SG")
        tmpRedistedInput = pullRedistribute(outArray,
                                            query,
                                            psLocalInstance,
                                            saveInstanceID,
                                            std::shared_ptr<CoordinateTranslator>(),
                                            0,
                                            std::shared_ptr<PartitioningSchemaData>());
        bool const wasConverted = (tmpRedistedInput != outArray) ;
        if (saveInstanceID == myInstanceID)
        {
            saveToDisk(tmpRedistedInput, settings.getFilePath(), query, settings.isBinaryFormat(), false);
        }
        if (wasConverted)
        {
            SynchableArray* syncArray = safe_dynamic_cast<SynchableArray*>(tmpRedistedInput.get());
            syncArray->sync();
        }
        return shared_ptr<Array>(new MemArray(_schema, query));
    }
};

REGISTER_PHYSICAL_OPERATOR_FACTORY(PhysicalAioSave, "aio_save", "PhysicalAioSave");


} // end namespace scidb
