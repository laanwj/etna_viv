# deobfuscation for alternative gcmXXX macros
# - Make u32 type configurable (for uapi)
# - Modify macros to take a bit field instead of register name and field
# - Add gcmGETFIELD
$bitfield = '\d+:\d+|[A-Z_a-z]+';
$id = '[^()]+|[^()]*(?:(\((?:[^()]++|(?-1))*+\))[^()]*)*';
$hex = '0x[0-9a-fA-F]+';
# 32-bit type
$u32 = 'u32';
#$u32 = 'gctUINT32';

$sub = "";
$name = "";
undef $prev;

while (<>) {
	s/\(0 ?\? ?($bitfield)\)/__gcmSTART(\1)/g;
	s/\(1 ?\? ?($bitfield)\)/__gcmEND(\1)/g;
	s/\(__gcmEND\(($bitfield)\) ?- ?__gcmSTART\(\1\) ?\+ ?1\)/__gcmGETSIZE(\1)/g;
	s/\(\($u32\) ?\(\(__gcmGETSIZE\(($bitfield)\) ?== ?32\) ?\? ?~0 ?: ?\(~\(~0 ?\<\< ?__gcmGETSIZE\((?-1)\)\)\)\)\)/__gcmMASK(\1)/g;
	s/\(\(\($u32\) ?\(($id)\)\) ?\<\< ?__gcmSTART\((?<field>$bitfield)\)\)/__gcmALIGN(\1, $+{field})/g;
        s/\( ?__gcmALIGN\(__gcmMASK\((?<field>$bitfield)\), ?(?-1)\) ?\)/gcmFIELDMASK($+{field})/g;
        s/\( ?\(\(\($u32\) ?\((?<data>$id)\)\) ?& ?~__gcmALIGN\(__gcmMASK\((?<field>$bitfield)\), ?\g{field}\)\) ?\| ?__gcmALIGN\(\($u32\) ?\((?<value>$id)\) ?& ?__gcmMASK\(\g{field}\), ?\g{field}\) ?\)/gcmSETFIELD($+{data}, $+{field}, $+{value})/g;
        # SETFIELD without u32 cast?!
        s/\( ?\(\(\($u32\) ?\((?<data>$id)\)\) ?& ?~__gcmALIGN\(__gcmMASK\((?<field>$bitfield)\), ?\g{field}\)\) ?\| ?__gcmALIGN\((?<value>$id) ?& ?__gcmMASK\(\g{field}\), ?\g{field}\) ?\)/gcmSETFIELD($+{data}, $+{field}, $+{value})/g;
        s/\( ?\(\(\($u32\) ?\((?<data>$id)\)\) ?\>\> ?__gcmSTART\((?<field>$bitfield)\) ?& ?__gcmMASK\(\g{field}\)\) ?== ?\((?<value>$id) ?& ?__gcmMASK\(\g{field}\) ?\) ?\)/gcmVERIFYFIELDVALUE($+{data}, $+{field}, $+{value})/g;

        s/\( ?\(\(\(\(u32\) ?\((?<data>$id)\)\) ?>> ?__gcmSTART\((?<field>$bitfield)\)\) ?& ?__gcmMASK\(\g{field}\)\) ?\)/gcmGETFIELD($+{data}, $+{field})/g;

	print;
}
