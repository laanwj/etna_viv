$bitfield = '\d+:\d+|[A-Z_a-z]+';
$id = '[^()]+|[^()]*(?:(\((?:[^()]++|(?-1))*+\))[^()]*)*';
$hex = '0x[0-9a-fA-F]+';

$sub = "";
$name = "";
undef $prev;

while (<>) {
	s/\(0 ?\? ?($bitfield)\)/__gcmSTART(\1)/g;
	s/\(1 ?\? ?($bitfield)\)/__gcmEND(\1)/g;
	s/\(__gcmEND\(($bitfield)\) ?- ?__gcmSTART\(\1\) ?\+ ?1\)/__gcmGETSIZE(\1)/g;
	s/\(\(gctUINT32\) ?\(\(__gcmGETSIZE\(($bitfield)\) ?== ?32\) ?\? ?~0 ?: ?\(~\(~0 ?<< ?__gcmGETSIZE\((?-1)\)\)\)\)\)/__gcmMASK(\1)/g;
	s/\(\(\(gctUINT32\) ?\(($id)\)\) ?\<\< ?__gcmSTART\((?<field>$bitfield)\)\)/__gcmALIGN(\1, $+{field})/g;

	print;
}
