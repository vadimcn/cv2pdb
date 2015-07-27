// Convert DMD CodeView/DWARF debug information to PDB files
// Copyright (c) 2009-2012 by Rainer Schuetze, All Rights Reserved
//
// License for redistribution is given by the Artistic License 2.0
// see file LICENSE for further details
//
// todo:
//  display associative array
//  64 bit:
//   - arguments passed by register
//   - real

#include "cv2pdb.h"
#include "PEImage.h"
#include "symutil.h"
#include "cvutil.h"

#include "dwarf.h"

#include <assert.h> 
#include <string>
#include <vector>


void CV2PDB::checkDWARFTypeAlloc(int size, int add)
{
	if (cbDwarfTypes + size > allocDwarfTypes)
	{
		//allocDwarfTypes += size + add;
		allocDwarfTypes += allocDwarfTypes/2 + size + add;
		dwarfTypes = (BYTE*) realloc(dwarfTypes, allocDwarfTypes);
		if (dwarfTypes == nullptr)
			__debugbreak();
	}
}

enum CV_X86_REG
{
	CV_REG_NONE = 0,
	CV_REG_EAX = 17,
	CV_REG_ECX = 18,
	CV_REG_EDX = 19,
	CV_REG_EBX = 20,
	CV_REG_ESP = 21,
	CV_REG_EBP = 22,
	CV_REG_ESI = 23,
	CV_REG_EDI = 24,
	CV_REG_ES = 25,
	CV_REG_CS = 26,
	CV_REG_SS = 27,
	CV_REG_DS = 28,
	CV_REG_FS = 29,
	CV_REG_GS = 30,
	CV_REG_IP = 31,
	CV_REG_FLAGS = 32,
	CV_REG_EIP = 33,
	CV_REG_EFLAGS = 34,
	CV_REG_ST0 = 128, /* this includes ST1 to ST7 */
	CV_REG_XMM0 = 154 /* this includes XMM1 to XMM7 */
};

CV_X86_REG dwarf_to_x86_reg(unsigned dwarf_reg)
{
	switch (dwarf_reg)
	{
		case  0: return CV_REG_EAX;
		case  1: return CV_REG_ECX;
		case  2: return CV_REG_EDX;
		case  3: return CV_REG_EBX;
		case  4: return CV_REG_ESP;
		case  5: return CV_REG_EBP;
		case  6: return CV_REG_ESI;
		case  7: return CV_REG_EDI;
		case  8: return CV_REG_EIP;
		case  9: return CV_REG_EFLAGS;
		case 10: return CV_REG_CS;
		case 11: return CV_REG_SS;
		case 12: return CV_REG_DS;
		case 13: return CV_REG_ES;
		case 14: return CV_REG_FS;
		case 15: return CV_REG_GS;
		case 16: case 17: case 18: case 19:
		case 20: case 21: case 22: case 23:
			return (CV_X86_REG)(CV_REG_ST0 + dwarf_reg - 16);
		case 32: case 33: case 34: case 35:
		case 36: case 37: case 38: case 39:
			return (CV_X86_REG)(CV_REG_XMM0 + dwarf_reg - 32);
		default:
			return CV_REG_NONE;
	}
}

void CV2PDB::appendStackVar(const char* name, int type, Location& loc)
{
	unsigned int len;
	unsigned int align = 4;
	checkUdtSymbolAlloc(100 + kMaxNameLen);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);

	CV_X86_REG baseReg = dwarf_to_x86_reg(loc.reg);
	if (baseReg == CV_REG_NONE)
		return;

	if (baseReg == CV_REG_EBP)
	{
		cvs->stack_v2.id = v3 ? S_BPREL_V3 : S_BPREL_V2;
		cvs->stack_v2.offset = loc.off;
		cvs->stack_v2.symtype = type;
		len = cstrcpy_v(v3, (BYTE*)&cvs->stack_v2.p_name, name);
		len += (BYTE*)&cvs->stack_v2.p_name - (BYTE*)cvs;
	}
	else
	{
		cvs->regrel_v3.id = S_REGREL_V3;
		cvs->regrel_v3.reg = baseReg;
		cvs->regrel_v3.offset = loc.off;
		cvs->regrel_v3.symtype = type;
		len = cstrcpy_v(true, (BYTE*)cvs->regrel_v3.name, name);
		len += (BYTE*)&cvs->regrel_v3.name - (BYTE*)cvs;
	}
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->stack_v2.len = len - 2;
	cbUdtSymbols += len;
}

void CV2PDB::appendGlobalVar(const char* name, int type, int seg, int offset)
{
	unsigned int len;
	unsigned int align = 4;

	for(char* cname = (char*) name; *cname; cname++)
		if (*cname == '.')
			*cname = dotReplacementChar;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->data_v2.id = v3 ? S_GDATA_V3 : S_GDATA_V2;
	cvs->data_v2.offset = offset;
	cvs->data_v2.symtype = type;
	cvs->data_v2.segment = seg;
	len = cstrcpy_v (v3, (BYTE*) &cvs->data_v2.p_name, name);
	len += (BYTE*) &cvs->data_v2.p_name - (BYTE*) cvs;
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->data_v2.len = len - 2;
	cbUdtSymbols += len;
}

bool CV2PDB::appendEndArg()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->generic.id = S_ENDARG_V1;
	cvs->generic.len = 2;
	cbUdtSymbols += 4;
	return true;
}

void CV2PDB::appendEnd()
{
	checkUdtSymbolAlloc(8);

	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->generic.id = S_END_V1;
	cvs->generic.len = 2;
	cbUdtSymbols += 4;
}

void CV2PDB::appendLexicalBlock(unsigned pclo, unsigned pchi)
{
	checkUdtSymbolAlloc(32);

	codeview_symbol*dsym = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	dsym->block_v3.id = S_BLOCK_V3;
	dsym->block_v3.parent = 0;
	dsym->block_v3.end = 0; // destSize + sizeof(dsym->block_v3) + 12;
	dsym->block_v3.length = pchi - pclo;
	dsym->block_v3.offset = pclo - codeSegOff;
	dsym->block_v3.segment = img.codeSegment + 1;
	dsym->block_v3.name[0] = 0;
	int len = sizeof(dsym->block_v3);
	for (; len & 3; len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	dsym->block_v3.len = len - 2;
	cbUdtSymbols += len;
}

bool CV2PDB::addDWARFProc(DWARF_InfoData& procid, DWARF_CompilationUnit* cu, DIECursor cursor)
{
	unsigned int pclo = procid.pclo - codeSegOff;
	unsigned int pchi = procid.pchi - codeSegOff;

	unsigned int len;
	unsigned int align = 4;

	checkUdtSymbolAlloc(100 + kMaxNameLen);

	// GLOBALPROC
	codeview_symbol*cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->proc_v2.id = v3 ? S_GPROC_V3 : S_GPROC_V2;
	cvs->proc_v2.pparent  = 0;
	cvs->proc_v2.pend     = 0;
	cvs->proc_v2.next     = 0;
	cvs->proc_v2.proc_len = pchi - pclo;
	cvs->proc_v2.debug_start = pclo - pclo;
	cvs->proc_v2.debug_end   = pchi - pclo;
	cvs->proc_v2.offset   = pclo;
	cvs->proc_v2.segment  = img.codeSegment + 1;
	cvs->proc_v2.proctype = 0; // translateType(sym->proc_v1.proctype);
	cvs->proc_v2.flags    = 0;

//    printf("GlobalPROC %s\n", procid.name);

	len = cstrcpy_v (v3, (BYTE*) &cvs->proc_v2.p_name, procid.name);
	len += (BYTE*) &cvs->proc_v2.p_name - (BYTE*) cvs;
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->proc_v2.len = len - 2;
	cbUdtSymbols += len;

#if 0 // add funcinfo
	cvs = (codeview_symbol*) (udtSymbols + cbUdtSymbols);
	cvs->funcinfo_32.id = S_FUNCINFO_32;
	cvs->funcinfo_32.sizeLocals = 20;
	memset(cvs->funcinfo_32.unknown, 0, sizeof(cvs->funcinfo_32.unknown));
	cvs->funcinfo_32.unknown[5] = 4;
	cvs->funcinfo_32.info = 0x4200;
	cvs->funcinfo_32.unknown2 = 0x11;
	len = sizeof(cvs->funcinfo_32);
	for (; len & (align-1); len++)
		udtSymbols[cbUdtSymbols + len] = 0xf4 - (len & 3);
	cvs->funcinfo_32.len = len - 2;
	cbUdtSymbols += len;
#endif

#if 0
	addStackVar("local_var", 0x1001, 8);
#endif

	Location frameBase = decodeLocation(procid.frame_base);
	if (cu)
	{
		DWARF_InfoData id;
		DIECursor prev = cursor;
		while (cursor.readSibling(id) && id.tag == DW_TAG_formal_parameter)
		{
			if (id.tag == DW_TAG_formal_parameter)
			{
				if (id.name && id.location.type == ExprLoc)
				{
					Location loc = decodeLocation(id.location, &frameBase);
					if (loc.is_regrel())
						appendStackVar(id.name, getTypeByDWARFPtr(cu, id.type), loc);
				}
			}
			prev = cursor;
		}
		appendEndArg();

        addLexicalBlocks(cu, prev, frameBase);
        appendEnd();
	}
	else
	{
		appendEndArg();
		appendEnd();
	}

	return true;
}

bool CV2PDB::addLexicalBlocks(DWARF_CompilationUnit* cu, DIECursor cursor, Location frameBase)
{
    DWARF_InfoData id;
    while (cursor.readSibling(id))
    {
        if (id.tag == DW_TAG_variable)
        {
            if (id.name && id.location.type == ExprLoc)
            {
                Location loc = decodeLocation(id.location, &frameBase);
                if (loc.is_regrel())
                    appendStackVar(id.name, getTypeByDWARFPtr(cu, id.type), loc);
            }
        }
        else if (id.tag == DW_TAG_lexical_block)
        {
            if (id.hasChild)
            {
                if (id.ranges != -1)
                {
                    // iterate over all code ranges
                    unsigned char* r = (unsigned char*)img.debug_ranges + id.ranges;
                    unsigned char* rend = (unsigned char*)img.debug_ranges + img.debug_ranges_length;
                    while (r < rend)
                    {
                        unsigned long pclo = RD4(r);
                        unsigned long pchi = RD4(r);
                        if (pclo == 0 && pchi == 0)
                            break;

                        appendLexicalBlock(pclo, pchi);
                        addLexicalBlocks(cu, cursor.getSubtreeCursor(), frameBase);
                        appendEnd();
                    }
                }
                else if (id.pchi != id.pclo)
                {
                    appendLexicalBlock(id.pclo, id.pchi);
                    addLexicalBlocks(cu, cursor.getSubtreeCursor(), frameBase);
                    appendEnd();
                }
            }
        }
    }

    return true;
}

int CV2PDB::addDWARFStructure(DWARF_InfoData& structid, DWARF_CompilationUnit* cu, DIECursor cursor)
{
	//printf("Adding struct %s, entryoff %d, abbrev %d\n", structid.name, structid.entryOff, structid.abbrev);

	bool isunion = structid.tag == DW_TAG_union_type;

	int fieldlistType = 0;
	int nfields = 0;
	if (cu)
	{
		checkDWARFTypeAlloc(100);
		codeview_reftype* fl = (codeview_reftype*) (dwarfTypes + cbDwarfTypes);
		int flbegin = cbDwarfTypes;
		fl->fieldlist.id = LF_FIELDLIST_V2;
		cbDwarfTypes += 4;

#if 0
		if(structid.containing_type && structid.containing_type != structid.entryOff)
		{
			codeview_fieldtype* bc = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
			bc->bclass_v2.id = LF_BCLASS_V2;
			bc->bclass_v2.offset = 0;
			bc->bclass_v2.type = getTypeByDWARFPtr(cu, structid.containing_type);
			bc->bclass_v2.attribute = 3; // public
			cbDwarfTypes += sizeof(bc->bclass_v2);
			for (; cbDwarfTypes & 3; cbDwarfTypes++)
				dwarfTypes[cbDwarfTypes] = 0xf4 - (cbDwarfTypes & 3);
			nfields++;
		}
#endif
		DWARF_InfoData id;
		int len = 0;
		while (cursor.readSibling(id))
		{
			int cvid = -1;
			if (id.tag == DW_TAG_member && id.name)
			{
				//printf("    Adding field %s\n", id.name);
				int off = 0;
				if (!isunion)
				{
					Location loc = decodeLocation(id.member_location);
					if (loc.is_abs())
					{
						off = loc.off;
						cvid = S_CONSTANT_V2;
					}
				}

				if(isunion || cvid == S_CONSTANT_V2)
				{
					checkDWARFTypeAlloc(kMaxNameLen + 100);
					codeview_fieldtype* dfieldtype = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
					cbDwarfTypes += addFieldMember(dfieldtype, 0, off, getTypeByDWARFPtr(cu, id.type), id.name);
					nfields++;
				}
			}
			else if(id.tag == DW_TAG_inheritance)
			{
				int off;
				Location loc = decodeLocation(id.member_location);
				if (loc.is_abs())
				{   
					cvid = S_CONSTANT_V2;
					off = loc.off;
				}
				if(cvid == S_CONSTANT_V2)
				{
					codeview_fieldtype* bc = (codeview_fieldtype*) (dwarfTypes + cbDwarfTypes);
					bc->bclass_v2.id = LF_BCLASS_V2;
					bc->bclass_v2.offset = off;
					bc->bclass_v2.type = getTypeByDWARFPtr(cu, id.type);
					bc->bclass_v2.attribute = 3; // public
					cbDwarfTypes += sizeof(bc->bclass_v2);
					for (; cbDwarfTypes & 3; cbDwarfTypes++)
						dwarfTypes[cbDwarfTypes] = 0xf4 - (cbDwarfTypes & 3);
					nfields++;
				}
			}
		}
		fl = (codeview_reftype*) (dwarfTypes + flbegin);
		fl->fieldlist.len = cbDwarfTypes - flbegin - 2;
		fieldlistType = nextDwarfType++;
	}

	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*) (userTypes + cbUserTypes);

	const char* name = (structid.name ? structid.name : "__noname");
	int attr = fieldlistType ? 0 : kPropIncomplete;
	int len = addAggregate(cvt, false, nfields, fieldlistType, attr, 0, 0, structid.byte_size, name);
	cbUserTypes += len;

	//ensureUDT()?
	int cvtype = nextUserType++;
	addUdtSymbol(cvtype, name);
	return cvtype;
}

int CV2PDB::getDWARFArrayBounds(DWARF_InfoData& arrayid, DWARF_CompilationUnit* cu, 
								DIECursor cursor, int& upperBound)
{
	int lowerBound = 0;

	if (cu)
	{
		DWARF_InfoData id;
		while (cursor.readSibling(id))
		{
			int cvid = -1;
			if (id.tag == DW_TAG_subrange_type)
			{
				lowerBound = id.lower_bound;
				upperBound = id.upper_bound;
			}
		}
	}
	return lowerBound;
}

int CV2PDB::addDWARFArray(DWARF_InfoData& arrayid, DWARF_CompilationUnit* cu, 
						  DIECursor cursor)
{
	int upperBound, lowerBound = getDWARFArrayBounds(arrayid, cu, cursor, upperBound);
	
	checkUserTypeAlloc(kMaxNameLen + 100);
	codeview_type* cvt = (codeview_type*) (userTypes + cbUserTypes);

	cvt->array_v2.id = v3 ? LF_ARRAY_V3 : LF_ARRAY_V2;
	cvt->array_v2.elemtype = getTypeByDWARFPtr(cu, arrayid.type);
	cvt->array_v2.idxtype = 0x74;
	int len = (BYTE*)&cvt->array_v2.arrlen - (BYTE*)cvt;
	int size = (upperBound - lowerBound + 1) * getDWARFTypeSize(cu, arrayid.type);
	len += write_numeric_leaf(size, &cvt->array_v2.arrlen);
	((BYTE*)cvt)[len++] = 0; // empty name
	for (; len & 3; len++)
		userTypes[cbUserTypes + len] = 0xf4 - (len & 3);
	cvt->array_v2.len = len - 2;

	cbUserTypes += len;

	int cvtype = nextUserType++;
	return cvtype;
}

bool CV2PDB::addDWARFTypes()
{
	checkUdtSymbolAlloc(100);

	int prefix = 4;
	DWORD* ddata = new DWORD [img.debug_info_length/4]; // large enough
	unsigned char *data = (unsigned char*) (ddata + prefix);
	unsigned int off = 0;
	unsigned int len;
	unsigned int align = 4;

	// SSEARCH
	codeview_symbol* cvs = (codeview_symbol*) (data + off);
	cvs->ssearch_v1.id = S_SSEARCH_V1;
	cvs->ssearch_v1.segment = img.codeSegment + 1;
	cvs->ssearch_v1.offset = 0;
	len = sizeof(cvs->ssearch_v1);
	for (; len & (align-1); len++)
		data[off + len] = 0xf4 - (len & 3);
	cvs->ssearch_v1.len = len - 2;
	off += len;

	// COMPILAND
	cvs = (codeview_symbol*) (data + off);
	cvs->compiland_v1.id = S_COMPILAND_V1;
	cvs->compiland_v1.unknown = 0x800100; // ?, 0x100: C++, 
	cvs->compiland_v1.unknown |= img.isX64() ? 0xd0 : 6; //0x06: Pentium Pro/II, 0xd0: x64
	len = sizeof(cvs->compiland_v1) - sizeof(cvs->compiland_v1.p_name);
	len += c2p("cv2pdb", cvs->compiland_v1.p_name);
	for (; len & (align-1); len++)
		data[off + len] = 0xf4 - (len & 3);
	cvs->compiland_v1.len = len - 2;
	off += len;

#if 0
	// define one proc over everything
	int s = codeSegment;
	int pclo = 0; // img.getImageBase() + img.getSection(s).VirtualAddress;
	int pchi = pclo + img.getSection(s).Misc.VirtualSize;
	addDWARFProc("procall", pclo, pchi, 0, 0, 0);
#endif

	//////////////////////////
	mspdb::Mod* mod = globalMod();
	//return writeSymbols (mod, ddata, off, prefix, true);
	return addSymbols (mod, data, off, true);
}

bool CV2PDB::addDWARFSectionContrib(mspdb::Mod* mod, unsigned long pclo, unsigned long pchi)
{
	int segIndex = img.findSection(pclo);
	if(segIndex >= 0)
	{
		int segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
		int rc = mod->AddSecContrib(segIndex, pclo, pchi - pclo, segFlags);
		if (rc <= 0)
			return setError("cannot add section contribution to module");
	}
	return true;
}

int CV2PDB::addDWARFBasicType(const char*name, int encoding, int byte_size)
{
	int type = 0, mode = 0, size = 0;
	switch(encoding)
	{
	case DW_ATE_boolean:        type = 3; break;
	case DW_ATE_complex_float:  type = 5; byte_size /= 2; break;
	case DW_ATE_float:          type = 4; break;
	case DW_ATE_signed:         type = 1; break;
	case DW_ATE_signed_char:    type = 7; break;
	case DW_ATE_unsigned:       type = 2; break;
	case DW_ATE_unsigned_char:  type = 7; break;
	case DW_ATE_imaginary_float:type = 4; break;
	default:
		setError("unknown basic type encoding");
	}
	switch(type)
	{
	case 1: // signed
	case 2: // unsigned
	case 3: // boolean
		switch(byte_size)
		{
		case 1: size = 0; break;
		case 2: size = 1; break;
		case 4: size = 2; break;
		case 8: size = 3; break;
		default:
			setError("unsupported integer type size");
		}
		break;
	case 4:
	case 5:
		switch(byte_size)
		{
		case 4:  size = 0; break;
		case 8:  size = 1; break;
		case 10: size = 2; break;
		case 12: size = 2; break; // with padding bytes
		case 16: size = 3; break;
		case 6:  size = 4; break;
		default:
			setError("unsupported real type size");
		}
		break;
	case 7:
		switch(byte_size)
		{
		case 1:  size = 0; break;
		case 2:  size = encoding == DW_ATE_signed_char ? 2 : 3; break;
		case 4:  size = encoding == DW_ATE_signed_char ? 4 : 5; break;
		case 8:  size = encoding == DW_ATE_signed_char ? 6 : 7; break;
		default:
			setError("unsupported real int type size");
		}
	}
	int t = size | (type << 4);
	t = translateType(t);
	int cvtype = appendTypedef(t, name, false);
	if(useTypedefEnum)
		addUdtSymbol(cvtype, name);
	return cvtype;
}

int CV2PDB::getTypeByDWARFPtr(DWARF_CompilationUnit* cu, byte* ptr)
{
	std::unordered_map<byte*, int>::iterator it = mapOffsetToType.find(ptr);
	if(it == mapOffsetToType.end())
		return 0x03; // void
	return it->second;
}

int CV2PDB::getDWARFTypeSize(DWARF_CompilationUnit* cu, byte* typePtr)
{
	DWARF_InfoData id;
	DIECursor cursor(cu, typePtr);

	if (!cursor.readNext(id))
		return 0;

	if(id.byte_size > 0)
		return id.byte_size;

	switch(id.tag)
	{
		case DW_TAG_ptr_to_member_type:
		case DW_TAG_reference_type:
		case DW_TAG_pointer_type:
			return cu->address_size;
		case DW_TAG_array_type:
		{
			int upperBound, lowerBound = getDWARFArrayBounds(id, cu, cursor, upperBound);
			return (upperBound + lowerBound + 1) * getDWARFTypeSize(cu, id.type);
		}
		default:
			if(id.type)
				return getDWARFTypeSize(cu, id.type);
			break;
	}
	return 0;
}

bool CV2PDB::mapTypes()
{
	int typeID = nextUserType;
	unsigned long off = 0;
	while (off < img.debug_info_length)
	{
		DWARF_CompilationUnit* cu = (DWARF_CompilationUnit*)(img.debug_info + off);

		DIECursor cursor(cu, (byte*)cu + sizeof(DWARF_CompilationUnit));
		DWARF_InfoData id;
		while (cursor.readNext(id))
		{
			//printf("0x%08x, level = %d, id.code = %d, id.tag = %d\n",
			//    (unsigned char*)cu + id.entryOff - (unsigned char*)img.debug_info, cursor.level, id.code, id.tag);
			switch (id.tag)
			{
				case DW_TAG_base_type:
				case DW_TAG_typedef:
				case DW_TAG_pointer_type:
				case DW_TAG_subroutine_type:
				case DW_TAG_array_type:
				case DW_TAG_const_type:
				case DW_TAG_structure_type:
				case DW_TAG_reference_type:

				case DW_TAG_class_type:
				case DW_TAG_enumeration_type:
				case DW_TAG_string_type:
				case DW_TAG_union_type:
				case DW_TAG_ptr_to_member_type:
				case DW_TAG_set_type:
				case DW_TAG_subrange_type:
				case DW_TAG_file_type:
				case DW_TAG_packed_type:
				case DW_TAG_thrown_type:
				case DW_TAG_volatile_type:
				case DW_TAG_restrict_type: // DWARF3
				case DW_TAG_interface_type:
				case DW_TAG_unspecified_type:
				case DW_TAG_mutable_type: // withdrawn
				case DW_TAG_shared_type:
				case DW_TAG_rvalue_reference_type:
					mapOffsetToType.insert(std::make_pair(id.entryPtr, typeID));
					typeID++;
			}
		}

		off += sizeof(cu->unit_length) + cu->unit_length;
	}

	nextDwarfType = typeID;
	return true;
}

bool CV2PDB::createTypes()
{
	mspdb::Mod* mod = globalMod();
	int typeID = nextUserType;
	int pointerAttr = img.isX64() ? 0x1000C : 0x800A;

	unsigned long off = 0;
	while (off < img.debug_info_length)
	{
		DWARF_CompilationUnit* cu = (DWARF_CompilationUnit*)(img.debug_info + off);

		DIECursor cursor(cu, (byte*)cu + sizeof(DWARF_CompilationUnit));
		DWARF_InfoData id;
		while (cursor.readNext(id))
		{
			//printf("0x%08x, level = %d, id.code = %d, id.tag = %d\n",
			//    (unsigned char*)cu + id.entryOff - (unsigned char*)img.debug_info, cursor.level, id.code, id.tag);

			if (id.specification)
			{
				DIECursor specCursor(cu, id.specification);
				DWARF_InfoData idspec;
				specCursor.readNext(idspec);
				assert(id.tag == idspec.tag);
				id.merge(idspec);
			}

			int cvtype = -1;
			switch (id.tag)
			{
			case DW_TAG_base_type:
				cvtype = addDWARFBasicType(id.name, id.encoding, id.byte_size);
				break;
			case DW_TAG_typedef:
				cvtype = appendModifierType(getTypeByDWARFPtr(cu, id.type), 0);
				addUdtSymbol(cvtype, id.name);
				break;
			case DW_TAG_pointer_type:
				cvtype = appendPointerType(getTypeByDWARFPtr(cu, id.type), pointerAttr);
				break;
			case DW_TAG_const_type:
				cvtype = appendModifierType(getTypeByDWARFPtr(cu, id.type), 1);
				break;
			case DW_TAG_reference_type:
				cvtype = appendPointerType(getTypeByDWARFPtr(cu, id.type), pointerAttr | 0x20);
				break;

			case DW_TAG_class_type:
			case DW_TAG_structure_type:
			case DW_TAG_union_type:
				cvtype = addDWARFStructure(id, cu, cursor.getSubtreeCursor());
				break;
			case DW_TAG_array_type:
				cvtype = addDWARFArray(id, cu, cursor.getSubtreeCursor());
				break;
			case DW_TAG_subroutine_type:
			case DW_TAG_subrange_type:

			case DW_TAG_enumeration_type:
			case DW_TAG_string_type:
			case DW_TAG_ptr_to_member_type:
			case DW_TAG_set_type:
			case DW_TAG_file_type:
			case DW_TAG_packed_type:
			case DW_TAG_thrown_type:
			case DW_TAG_volatile_type:
			case DW_TAG_restrict_type: // DWARF3
			case DW_TAG_interface_type:
			case DW_TAG_unspecified_type:
			case DW_TAG_mutable_type: // withdrawn
			case DW_TAG_shared_type:
			case DW_TAG_rvalue_reference_type:
				cvtype = appendPointerType(0x74, pointerAttr);
				break;

			case DW_TAG_subprogram:
				if (id.name && id.pclo && id.pchi)
				{
					addDWARFProc(id, cu, cursor.getSubtreeCursor());
					int rc = mod->AddPublic2(id.name, img.codeSegment + 1, id.pclo - codeSegOff, 0);
				}
				break;

			case DW_TAG_compile_unit:
#if !FULL_CONTRIB
				if (id.dir && id.name)
				{
					if (id.ranges != -1 && id.ranges < img.debug_ranges_length)
					{
						unsigned char* r = (unsigned char*)img.debug_ranges + id.ranges;
						unsigned char* rend = (unsigned char*)img.debug_ranges + img.debug_ranges_length;
						while (r < rend)
						{
							unsigned long pclo = RD4(r);
							unsigned long pchi = RD4(r);
							if (pclo == 0 && pchi == 0)
								break;
							//printf("%s %s %x - %x\n", dir, name, pclo, pchi);
							if (!addDWARFSectionContrib(mod, pclo, pchi))
								return false;
						}
					}
					else
					{
						//printf("%s %s %x - %x\n", dir, name, pclo, pchi);
						if (!addDWARFSectionContrib(mod, id.pclo, id.pchi))
							return false;
					}
				}
#endif
				break;

			case DW_TAG_variable:
				if (id.name)
				{
					int seg = -1;
					unsigned long segOff;
					if (id.location.type == Invalid && id.external && id.linkage_name)
					{
						seg = img.findSymbol(id.linkage_name, segOff);
					}
					else
					{
						Location loc = decodeLocation(id.location);
						if (loc.is_abs())
						{
							segOff = loc.off;
							seg = img.findSection(segOff);
							if (seg >= 0)
								segOff -= img.getImageBase() + img.getSection(seg).VirtualAddress;
						}
					}
					if (seg >= 0)
					{
						int type = getTypeByDWARFPtr(cu, id.type);
						appendGlobalVar(id.name, type, seg + 1, segOff);
						int rc = mod->AddPublic2(id.name, seg + 1, segOff, type);
					}
				}
				break;
			case DW_TAG_formal_parameter:
			case DW_TAG_unspecified_parameters:
			case DW_TAG_inheritance:
			case DW_TAG_member:
			case DW_TAG_inlined_subroutine:
			case DW_TAG_lexical_block:
			default:
				break;
			}

			if (cvtype >= 0)
			{
				assert(cvtype == typeID); typeID++;
				assert(mapOffsetToType[id.entryPtr] == cvtype);
			}
		}

		off += sizeof(cu->unit_length) + cu->unit_length;
	}

	return true;
}

bool CV2PDB::createDWARFModules()
{
	if(!img.debug_info)
		return setError("no .debug_info section found");

	codeSegOff = img.getImageBase() + img.getSection(img.codeSegment).VirtualAddress;

	mspdb::Mod* mod = globalMod();
	for (int s = 0; s < img.countSections(); s++)
	{
		const IMAGE_SECTION_HEADER& sec = img.getSection(s);
		int rc = dbi->AddSec(s + 1, 0x10d, 0, sec.SizeOfRawData);
		if (rc <= 0)
			return setError("cannot add section");
	}

#define FULL_CONTRIB 1
#if FULL_CONTRIB
	// we use a single global module, so we can simply add the whole text segment
	int segFlags = 0x60101020; // 0x40401040, 0x60500020; // TODO
	int s = img.codeSegment;
	int pclo = 0; // img.getImageBase() + img.getSection(s).VirtualAddress;
	int pchi = pclo + img.getSection(s).Misc.VirtualSize;
	int rc = mod->AddSecContrib(s + 1, pclo, pchi - pclo, segFlags);
	if (rc <= 0)
		return setError("cannot add section contribution to module");
#endif

	checkUserTypeAlloc();
	*(DWORD*) userTypes = 4;
	cbUserTypes = 4;

	createEmptyFieldListType();
	if(Dversion > 0)
	{
		appendComplex(0x50, 0x40, 4, "cfloat");
		appendComplex(0x51, 0x41, 8, "cdouble");
		appendComplex(0x52, 0x42, 12, "creal");
	}

	DIECursor::setContext(&img);

	countEntries = 0;
	if (!mapTypes())
		return false;
	if (!createTypes())
		return false;

	/*
	if(!iterateDWARFDebugInfo(kOpMapTypes))
		return false;
	if(!iterateDWARFDebugInfo(kOpCreateTypes))
		return false;
	*/

#if 0
	modules = new mspdb::Mod* [countEntries];
	memset (modules, 0, countEntries * sizeof(*modules));

	for (int m = 0; m < countEntries; m++)
	{
		mspdb::Mod* mod = globalMod();
	}
#endif

	if(cbUserTypes > 0 || cbDwarfTypes)
	{
		if(dwarfTypes)
		{
			checkUserTypeAlloc(cbDwarfTypes);
			memcpy(userTypes + cbUserTypes, dwarfTypes, cbDwarfTypes);
			cbUserTypes += cbDwarfTypes;
			cbDwarfTypes = 0;
		}
		int rc = mod->AddTypes(userTypes, cbUserTypes);
		if (rc <= 0)
			return setError("cannot add type info to module");
	}
	return true;
}

bool isRelativePath(const std::string& s)
{
	if(s.length() < 1)
		return true;
	if(s[0] == '/' || s[0] == '\\')
		return false;
	if(s.length() < 2)
		return true;
	if(s[1] == ':')
		return false;
	return true;
}

static int cmpAdr(const void* s1, const void* s2)
{
	const mspdb::LineInfoEntry* e1 = (const mspdb::LineInfoEntry*) s1;
	const mspdb::LineInfoEntry* e2 = (const mspdb::LineInfoEntry*) s2;
	return e1->offset - e2->offset;
}

bool _flushDWARFLines(CV2PDB* cv2pdb, DWARF_LineNumberProgramHeader& hdr, DWARF_LineState& state)
{
    //if (!strcmp(hdr.file_names[0].file_name, "loop.rs"))
    //    __debugbreak();

	if(state.lineInfo.size() == 0)
		return true;

	unsigned int saddr = state.lineInfo[0].offset;
	unsigned int eaddr = state.lineInfo.back().offset;
	int segIndex = cv2pdb->img.findSection(saddr + state.seg_offset);
	if(segIndex < 0)
	{
		// throw away invalid lines (mostly due to "set address to 0")
		state.lineInfo.resize(0);
		return true;
		//return false;
	}

//    if(saddr >= 0x4000)
//        return true;

	const DWARF_FileName* dfn;
	if(state.file > 0 && state.file <= hdr.file_names.size())
        dfn = &hdr.file_names[state.file - 1];
	else
		return false;
	std::string fname = dfn->file_name;
	
	if(isRelativePath(fname) && 
        dfn->dir_index > 0 && dfn->dir_index <= hdr.include_directories.size())
	{
        std::string dir = hdr.include_directories[dfn->dir_index - 1];
		if(dir.length() > 0 && dir[dir.length() - 1] != '/' && dir[dir.length() - 1] != '\\')
			dir.append("\\");
		fname = dir + fname;
	}
	for(size_t i = 0; i < fname.length(); i++)
		if(fname[i] == '/')
			fname[i] = '\\';

	mspdb::Mod* mod = cv2pdb->globalMod();
#if 1
	bool dump = false; // (fname == "cvtest.d");
	//qsort(&state.lineInfo[0], state.lineInfo.size(), sizeof(state.lineInfo[0]), cmpAdr);
#if 0
	printf("%s:\n", fname.c_str());
	for(size_t ln = 0; ln < state.lineInfo.size(); ln++)
		printf("  %08x: %4d\n", state.lineInfo[ln].offset + 0x401000, state.lineInfo[ln].line);
#endif

	unsigned int firstLine = state.lineInfo[0].line;
	unsigned int firstAddr = state.lineInfo[0].offset;
	unsigned int firstEntry = 0;
	unsigned int entry = 0;
	for(size_t ln = firstEntry; ln < state.lineInfo.size(); ln++)
	{
		if(state.lineInfo[ln].line < firstLine || state.lineInfo[ln].offset < firstAddr)
		{
			if(ln > firstEntry)
			{
				unsigned int length = state.lineInfo[entry-1].offset + 1; // firstAddr has been subtracted before
				if(dump)
					printf("AddLines(%08x+%04x, Line=%4d+%3d, %s)\n", firstAddr, length, firstLine, entry - firstEntry, fname.c_str());
				int rc = mod->AddLines(fname.c_str(), segIndex + 1, firstAddr, length, firstAddr, firstLine,
									   (unsigned char*) &state.lineInfo[firstEntry], 
									   (ln - firstEntry) * sizeof(state.lineInfo[0]));
				firstLine = state.lineInfo[ln].line;
				firstAddr = state.lineInfo[ln].offset;
				firstEntry = entry;
			}
		}
		else if(ln > firstEntry && state.lineInfo[ln].offset == state.lineInfo[ln-1].offset)
			continue; // skip entries without offset change
		state.lineInfo[entry].line = state.lineInfo[ln].line - firstLine;
		state.lineInfo[entry].offset = state.lineInfo[ln].offset - firstAddr;
		entry++;
	}
	unsigned int length = eaddr - firstAddr;
	if(dump)
		printf("AddLines(%08x+%04x, Line=%4d+%3d, %s)\n", firstAddr, length, firstLine, entry - firstEntry, fname.c_str());
	int rc = mod->AddLines(fname.c_str(), segIndex + 1, firstAddr, length, firstAddr, firstLine,
						   (unsigned char*) &state.lineInfo[firstEntry], 
						   (entry - firstEntry) * sizeof(state.lineInfo[0]));
#else
	unsigned int firstLine = 0;
	unsigned int firstAddr = 0;
	int rc = mod->AddLines(fname.c_str(), segIndex + 1, saddr, eaddr - saddr, firstAddr, firstLine,
						   (unsigned char*) &state.lineInfo[0], state.lineInfo.size() * sizeof(state.lineInfo[0]));
#endif

	state.lineInfo.resize(0);
	return rc > 0;
}

bool CV2PDB::addDWARFLines()
{
	if(!img.debug_line)
		return setError("no .debug_line section found");

	mspdb::Mod* mod = globalMod();
	for (uint64_t off = 0; off < img.debug_line_length; )
	{
        DWARF_LineNumberProgramHeader hdr;
        byte* p = (byte*)(img.debug_line + off);
		
        hdr.unit_length = RD4(p); // 12 byte in DWARF-64
        byte* end = p + hdr.unit_length; // end of line number info
        hdr.version = RD2(p);
        hdr.header_length = RD4(p); // 8 byte in DWARF-64
        byte* start = p + hdr.header_length; // start of line number info
        hdr.minimum_instruction_length = RD1(p);
        if (this->dwarfVersion >= 4) // new in DWARF4
            hdr.maximum_operations_per_instruction = RD1(p);
        else
            hdr.maximum_operations_per_instruction = 1;
        hdr.default_is_stmt = RD1(p);
        hdr.line_base = RD1(p);
        hdr.line_range = RD1(p);
        hdr.opcode_base = RD1(p);

        hdr.standard_opcode_lengths.resize(hdr.opcode_base);
        for (int o = 1; o < hdr.opcode_base; o++)
            hdr.standard_opcode_lengths[o] = LEB128(p);
        // dirs
        while (*p != 0)
        {
            hdr.include_directories.push_back((char*)p);
            p += strlen((const char*)p) + 1;
        }
        ++p;
        // files
        DWARF_FileName fname;
        while (*p)
        {
            fname.read(p);
            hdr.file_names.push_back(fname);
        }
        ++p;

        assert(p == start); // p should be positioned at the end of the header

        if (hdr.unit_length >= 0xfffffff0)
			break;

		DWARF_LineState state;
        state.init(hdr);
        state.seg_offset = img.getImageBase() + img.getSection(img.codeSegment).VirtualAddress;

		std::vector<mspdb::LineInfoEntry> lineInfo;
		while (p < end)
		{
			int opcode = *p++;
			if (opcode >= hdr.opcode_base)
			{
				// special opcode
				int adjusted_opcode = opcode - hdr.opcode_base;
				int operation_advance = adjusted_opcode / hdr.line_range;
				state.advance_addr(hdr, operation_advance);
				int line_advance = hdr.line_base + (adjusted_opcode % hdr.line_range);
				state.line += line_advance;

				state.addLineInfo();

				state.basic_block = false;
				state.prologue_end = false;
				state.epilogue_end = false;
				state.discriminator = 0;
			}
			else
			{
				switch(opcode)
				{
				case 0: // extended
					{
						int exlength = LEB128(p);
						unsigned char* q = p + exlength;
						int excode = *p++;
						switch(excode)
						{
						case DW_LNE_end_sequence:
							if((char*)p - img.debug_line >= 0xe4e0)
								p = p;
							state.end_sequence = true;
							state.last_addr = state.address;
							state.addLineInfo();
							state.init(hdr);
							break;
						case DW_LNE_set_address:
							if(unsigned long adr = RD4(p))
								state.address = adr;
							else
								state.address = state.last_addr; // strange adr 0 for templates?
							state.op_index = 0;
							break;
						case DW_LNE_define_file:
							fname.read(p);
                            hdr.file_names.push_back(fname);
							break;
						case DW_LNE_set_discriminator:
							state.discriminator = LEB128(p);
							break;
						}
						p = q;
					}
					break;
				case DW_LNS_copy:
					state.addLineInfo();
					state.basic_block = false;
					state.prologue_end = false;
					state.epilogue_end = false;
					state.discriminator = 0;
					break;
				case DW_LNS_advance_pc:
					state.advance_addr(hdr, LEB128(p));
					break;
				case DW_LNS_advance_line:
					state.line += SLEB128(p);
					break;
				case DW_LNS_set_file:
					state.file = LEB128(p);
					break;
				case DW_LNS_set_column:
					state.column = LEB128(p);
					break;
				case DW_LNS_negate_stmt:
					state.is_stmt = !state.is_stmt;
					break;
				case DW_LNS_set_basic_block:
					state.basic_block = true;
					break;
				case DW_LNS_const_add_pc:
					state.advance_addr(hdr, (255 - hdr.opcode_base) / hdr.line_range);
					break;
				case DW_LNS_fixed_advance_pc:
					state.address += RD2(p);
					state.op_index = 0;
					break;
				case DW_LNS_set_prologue_end:
					state.prologue_end = true;
					break;
				case DW_LNS_set_epilogue_begin:
					state.epilogue_end = true;
					break;
				case DW_LNS_set_isa:
					state.isa = LEB128(p);
					break;
				default:
					// unknown standard opcode
					for(int arg = 0; arg < hdr.standard_opcode_lengths[opcode]; arg++)
						LEB128(p);
					break;
				}
			}
		}

		if (!_flushDWARFLines(this, hdr, state))
			return setError("cannot add line number info to module");

        off = end - (byte*)img.debug_line;
	}

	return true;
}

bool CV2PDB::relocateDebugLineInfo()
{
	if(!img.reloc || !img.reloc_length)
		return true;

	unsigned int img_base = 0x400000;
	char* relocbase = img.reloc;
	char* relocend = img.reloc + img.reloc_length;
	while(relocbase < relocend)
	{
		unsigned int virtadr = *(unsigned int *) relocbase;
		unsigned int chksize = *(unsigned int *) (relocbase + 4);

		char* p = img.RVA<char> (virtadr, 1);
		if(p >= img.debug_line && p < img.debug_line + img.debug_line_length)
		{
			for (unsigned int w = 8; w < chksize; w += 2)
			{
				unsigned short entry = *(unsigned short*)(relocbase + w);
				unsigned short type = (entry >> 12) & 0xf;
				unsigned short off = entry & 0xfff;

				if(type == 3) // HIGHLOW
				{
					*(long*) (p + off) += img_base;
				}
			}
		}
		if(chksize == 0 || chksize >= img.reloc_length)
			break;
		relocbase += chksize;
	}
	return true;
}

bool CV2PDB::addDWARFPublics()
{
	mspdb::Mod* mod = globalMod();

	int type = 0;
	int rc = mod->AddPublic2("public_all", img.codeSegment + 1, 0, 0x1000);
	if (rc <= 0)
		return setError("cannot add public");
	return true;
}

bool CV2PDB::writeDWARFImage(const TCHAR* opath)
{
	int len = sizeof(*rsds) + strlen((char*)(rsds + 1)) + 1;
	if (!img.replaceDebugSection(rsds, len, false))
		return setError(img.getLastError());

	if (!img.save(opath))
		return setError(img.getLastError());

	return true;
}
