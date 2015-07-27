#ifndef __READDWARF_H__
#define __READDWARF_H__

#include <string>
#include <vector>
#include <cstdint>
#include "mspdb.h"

typedef unsigned char byte;

inline unsigned int LEB128(byte* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while (*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	p++;
	return x;
}

inline int SLEB128(byte* &p)
{
	unsigned int x = 0;
	int shift = 0;
	while (*p & 0x80)
	{
		x |= (*p & 0x7f) << shift;
		shift += 7;
		p++;
	}
	x |= *p << shift;
	if (*p & 0x40)
		x |= -(1 << (shift + 7)); // sign extend
	p++;
	return x;
}

inline byte RD1(byte* &p)
{
    return *p++;
}

inline unsigned int RD2(byte* &p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	return x;
}

inline unsigned int RD4(byte* &p)
{
	unsigned int x = *p++;
	x |= *p++ << 8;
	x |= *p++ << 16;
	x |= *p++ << 24;
	return x;
}

inline unsigned long long RD8(byte* &p)
{
	unsigned long long x = *p++;
	for (int shift = 8; shift < 64; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}

inline unsigned long long RDsize(byte* &p, int size)
{
	if (size > 8)
		size = 8;
	unsigned long long x = *p++;
	for (int shift = 8; shift < size * 8; shift += 8)
		x |= (unsigned long long) *p++ << shift;
	return x;
}

enum AttrClass
{
	Invalid,
	Addr,
	Block,
	Const,
	String,
	Flag,
	Ref,
	ExprLoc,
	SecOffset
};

struct DWARF_Attribute
{
	AttrClass type;
	union
	{
		unsigned long addr;
		struct { byte* ptr; unsigned len; } block;
		unsigned long cons;
		const char* string;
		bool flag;
		byte* ref;
		struct { byte* ptr; unsigned len; } expr;
		unsigned long sec_offset;
	};
};

///////////////////////////////////////////////////////////////////////////////

#include "pshpack1.h"

struct DWARF_CompilationUnit
{
	unsigned int unit_length; // 12 byte in DWARF-64
	unsigned short version;
	unsigned int debug_abbrev_offset; // 8 byte in DWARF-64
	byte address_size;

	bool isDWARF64() const { return unit_length == ~0; }
	int refSize() const { return unit_length == ~0 ? 8 : 4; }
};

struct DWARF_FileName
{
	const char* file_name;
	unsigned int  dir_index;
	unsigned long lastModification;
	unsigned long fileLength;

	void read(byte* &p)
	{
		file_name = (const char*)p;
		p += strlen((const char*)p) + 1;
		dir_index = LEB128(p);
		lastModification = LEB128(p);
		fileLength = LEB128(p);
	}
};

struct DWARF_InfoData
{
	byte* entryPtr;
	unsigned entryOff; // offset in the cu
	int code;
	byte* abbrev;
	int tag;
	int hasChild;

	const char* name;
	const char* linkage_name;
	const char* dir;
	unsigned long byte_size;
	byte* sibling;
	unsigned long encoding;
	unsigned long pclo;
	unsigned long pchi;
	unsigned long ranges;
	byte* type;
	byte* containing_type;
	byte* specification;
	unsigned long inlined;
	bool external;
	DWARF_Attribute location;
	DWARF_Attribute member_location;
	DWARF_Attribute frame_base;
	long upper_bound;
	long lower_bound;

	void clear()
	{
		entryPtr = 0;
		code = 0;
		abbrev = 0;
		tag = 0;
		hasChild = 0;

		name = 0;
		linkage_name = 0;
		dir = 0;
		byte_size = 0;
		sibling = 0;
		encoding = 0;
		pclo = 0;
		pchi = 0;
		ranges = -1;
		type = 0;
		containing_type = 0;
		specification = 0;
		inlined = 0;
		external = 0;
		member_location.type = Invalid;
		location.type = Invalid;
		frame_base.type = Invalid;
		upper_bound = 0;
		lower_bound = 0;
	}

	void merge(const DWARF_InfoData& id)
	{
		if (id.name) name = id.name;
		if (id.linkage_name) linkage_name = id.linkage_name;
		if (id.dir) dir = id.dir;
		if (id.byte_size) byte_size = id.byte_size;
		if (id.sibling) sibling = id.sibling;
		if (id.encoding) encoding = id.encoding;
		if (id.pclo) pclo = id.pclo;
		if (id.pchi) pchi = id.pchi;
		if (id.ranges != -1) ranges = id.ranges;
		if (id.type) type = id.type;
		if (id.containing_type) containing_type = id.containing_type;
		if (id.specification) specification = id.specification;
		if (id.inlined) inlined = id.inlined;
		if (id.external) external = id.external;
		if (id.member_location.type != Invalid) member_location = id.member_location;
		if (id.location.type != Invalid) location = id.location;
		if (id.frame_base.type != Invalid) frame_base = id.frame_base;
		if (id.upper_bound) upper_bound = id.upper_bound;
		if (id.lower_bound) lower_bound = id.lower_bound;
	}
};

struct DWARF_LineNumberProgramHeader
{
    uint64_t unit_length; // 12 byte in DWARF-64
    int version;
    uint64_t header_length; // 8 byte in DWARF-64
    byte minimum_instruction_length;
    byte maximum_operations_per_instruction; (// not in DWARF 2
    byte default_is_stmt;
    signed char line_base;
    byte line_range;
    byte opcode_base;
    std::vector<int> standard_opcode_lengths;
    std::vector<char*> include_directories;
    std::vector<DWARF_FileName> file_names;
};

struct DWARF_LineState
{
	unsigned long address;
	unsigned int  op_index;
	unsigned int  file;
	unsigned int  line;
	unsigned int  column;
	bool          is_stmt;
	bool          basic_block;
	bool          end_sequence;
	bool          prologue_end;
	bool          epilogue_end;
	unsigned int  isa;
	unsigned int  discriminator;

	// not part of the "documented" state
	unsigned long seg_offset;
	unsigned long last_addr;

    struct LineInfo
    {
        unsigned address;
        unsigned file;
        unsigned line;
    };
    std::vector<LineInfo> lineInfo;

    void init(DWARF_LineNumberProgramHeader& hdr)
	{
		address = 0;
		op_index = 0;
		file = 1;
		line = 1;
		column = 0;
		is_stmt = hdr.default_is_stmt != 0;
		basic_block = false;
		end_sequence = false;
		prologue_end = false;
		epilogue_end = false;
		isa = 0;
		discriminator = 0;
	}

    void advance_addr(DWARF_LineNumberProgramHeader& hdr, int operation_advance)
	{
		int address_advance = hdr.minimum_instruction_length * ((op_index + operation_advance) / hdr.maximum_operations_per_instruction);
		address += address_advance;
        op_index = (op_index + operation_advance) % hdr.maximum_operations_per_instruction;
	}

	void addLineInfo()
	{
        LineInfo entry;
        entry.address = address;
        entry.file = file;
		entry.line = line;
		lineInfo.push_back(entry);
	}
};

#include "poppack.h"

///////////////////////////////////////////////////////////////////////////////


struct Location
{
	enum Type
	{
		Invalid, // Failed to evaluate the location expression
		InReg,   // In register (reg)
		Abs,     // Absolute address (off)
		RegRel   // Register-relative address ($reg + off)
	};

	Type type;
	int reg;
	int off;

	bool is_invalid() const { return type == Invalid; }
	bool is_inreg() const { return type == InReg; }
	bool is_abs() const { return type == Abs; }
	bool is_regrel() const { return type == RegRel; }
};

// Attemots to partially evaluate DWARF location expressions.
// The only supported expressions are those, whose result may be represented
// as either an absolute value, a register, or a register-relative address.
Location decodeLocation(const DWARF_Attribute& attr, const Location* frameBase = 0);

class PEImage;

// Debug Information Entry Cursor
class DIECursor
{
public:
	DWARF_CompilationUnit* cu;
	byte* ptr;
	int level;
	bool hasChild; // indicates whether the last read DIE has children
	byte* sibling;

	byte* getDWARFAbbrev(unsigned off, unsigned findcode);

public:

	static void setContext(PEImage* img_);

	// Create a new DIECursor
	DIECursor(DWARF_CompilationUnit* cu_, byte* ptr);

	// Reads next sibling DIE.  If the last read DIE had any children, they will be skipped over.
	// Returns 'false' upon reaching the last sibling on the current level.
	bool readSibling(DWARF_InfoData& id);

	// Returns cursor that will enumerate children of the last read DIE.
	DIECursor getSubtreeCursor();

	// Reads the next DIE in physical order, returns 'true' if succeeds.
	// If stopAtNull is true, readNext() will stop upon reaching a null DIE (end of the current tree level).
	// Otherwise, it will skip null DIEs and stop only at the end of the subtree for which this DIECursor was created.
	bool readNext(DWARF_InfoData& id, bool stopAtNull = false);
};

#endif