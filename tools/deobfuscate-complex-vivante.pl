$bitfield = '\d+:\d+|[A-Z_a-z]+';
$id = '[^()]+|[^()]*(?:(\((?:[^()]++|(?-1))*+\))[^()]*)*';
$hex = '0x[0-9a-fA-F]+';

$sub = "";
$name = "";
undef $prev;

while (<>) {
	if (/^#define/) {
		if (/^#define\s+(\w+)\s+($hex|\d+)/) {
			my ($w, $v) = ($1, $2);
			$v = hex($v) if ($v =~ /^0x/);

			if ($w =~ /(gcCMD)_(\w+)_([^ 	]+)/) {
				$values{$1}{$2}{$v} = $3;
				$name = $3;
			} elsif ($w =~ /(GC_PROFILE_SEL[01])_(\w+)_([^ 	]+)/) {
				$values{$1}{$2}{$v} = $3;
			}
		}
		if (/\s+(\w+)_(\w+)\s+(\d+:\d+)/) {
			$field{$1}{$name}{$3} = $2;
#print ">>> '$1' '$name' '$3' = '$2'\n";
		}
		print;
		next;
	}

	$o = $_;
	$m = 0;

	s/\(0 ?\? ?($bitfield)\)/__gcmSTART(\1)/g;
	s/\(1 ?\? ?($bitfield)\)/__gcmEND(\1)/g;
	s/\(__gcmEND\(($bitfield)\) ?- ?__gcmSTART\(\1\) ?\+ ?1\)/__gcmGETSIZE(\1)/g;
	s/\(\(gctUINT32\) ?\(\(__gcmGETSIZE\(($bitfield)\) ?== ?32\) ?\? ?~0 ?: ?\(~\(~0 ?<< ?__gcmGETSIZE\((?-1)\)\)\)\)\)/__gcmMASK(\1)/g;

	$m |= s/\(\(\(gctUINT32\) ?\(($id)\)\) ?\<\< ?__gcmSTART\((?<field>$bitfield)\)\)/__gcmALIGN(\1, $+{field})/g;
	$m |= s/\(\(\(\(gctUINT32\) \(($id)\)\) >> __gcmSTART\((?<field>$bitfield)\) & __gcmMASK\(\k<field>\)\) == \((?<value>[0-9a-fx]+) & __gcmMASK\(\k<field>\)\)\)/gcmVERIFYFIELDVALUE(\1, ####, $+{field}, $+{value})/g;
	$m |= s/\( ?\(\(\(\(gctUINT32\) \(($id)\)\) >> __gcmSTART\((?<field>$bitfield)\)\) & __gcmMASK\(\k<field>\)\) ?\)/gcmGETFIELD(\1, ####, $+{field})/g;
	$m |= s/\(\(\(\(gctUINT32\) \(($id)\)\) & ~__gcmALIGN\(__gcmMASK\((?<field>$bitfield)\), \k<field>\)\) \| __gcmALIGN\(\(gctUINT32\) \((?<value>$id)\) & __gcmMASK\(\k<field>\), \k<field>\)\)/gcmSETFIELD(\1, ####, $+{field}, $+{value})/g;
	$m |= s/\(\(\(\(gctUINT32\) \(($id)\)\) & ~__gcmALIGN\(__gcmMASK\((?<field>$bitfield)\), \k<field>\)\) \| __gcmALIGN\((?<value>$id) ?& ?__gcmMASK\(\k<field>\), \k<field>\)\)/gcmSETFIELDVALUE(\1, ####, $+{field}, $+{value})/g;

	if ($m and /####/) {
		# Now fix things up a bit.  First, the spec field definition
		s/(gcmGETFIELD\(specs, )####, GC_CHIP_SPECS_field\)/\1GC_CHIP_SPECS, field)/g;
#		s/(= gcmGETFIELD\(specs, )####, /\1GC_CHIP_SPECS, /;

		# Now, replace command opcode fields
		if (/gcmSETFIELDVALUE\(0, ####, 31:27,/) {
			$reg = "gcCMD";
			$sub = "";
		}
		if (/\(State, ####, 31:27,/) {
			$reg = "gcCMD";
			$sub = "";
		}
		if (/\(specs, ####, /) {
			$reg = "GC_CHIP_SPECS";
			$sub = "";
		}
		if (/\(chipIdentity, ####, /) {
			$reg = "GC_CHIP_IDENTITY";
			$sub = "";
		}
		if (/\((?:features|\*ChipFeatures|.*->chipFeatures), ####, /) {
			$reg = "GC_CHIP_FEATURES";
			$sub = "";
		}
		if (/[Cc]hipMinorFeatures0, ####, /) {
			$reg = "GC_CHIP_MINOR_FEATURES0";
			$sub = "";
		}
		if (/[Cc]hipMinorFeatures1, ####, /) {
			$reg = "GC_CHIP_MINOR_FEATURES1";
			$sub = "";
		}
		if (/\(idle, ####, /) {
			$reg = "GC_IDLE";
			$sub = "";
		}
		if (/debug, ####, /) {
			$reg = "AQMemoryDebug";
			$sub = "";
		}
		if (/destination/) {
			$reg = "AQEvent";
			$sub = "";
		}
		if (/Address/) {
			$reg = "ADDRESS";
			$sub = "";
		}
		if (/gckOS_WriteRegister.*0x0+470/) {
			$reg = "GC_DEBUG_CONTROL0";
			$sub = "";
		}
		if (/gckOS_WriteRegister.*0x0+474/) {
			$reg = "GC_DEBUG_CONTROL1";
			$sub = "";
		}
		if (/gckOS_WriteRegister.*0x0+478/) {
			$reg = "GC_DEBUG_CONTROL2";
			$sub = "";
		}
		if (/if \(\(gcmGETFIELD\(control/) {
			$reg = "GC_CLOCK_CONTROL";
			$sub = "";
		}
		if (/stall =/) {
			$reg = "STALL";
			$sub = "";
		}

		# Replace register names
		s/####/$reg/ if defined $reg;

		# Replace bitfields with their definitions
		if (/(\w+), (\d+:\d+)/) {
			my $r = $1;
			my $f = $2;
			if (defined($field{$r}{$sub}{$f})) {
				s/$r, $f/$r, $field{$r}{$sub}{$f}/;
			}
		}

		# Replace command opcodes with their decimal equivalents
		s/($reg, \w+, )($hex) */$1.hex($2)/eg;
		if (/(\w+), (\w+), (\d+) */ and defined($values{$1}{$2}{$3})) {
#print ">>> $1 $2 $3\n";
			$sub = $values{$1}{$2}{$3} if $sub eq "";
			s/((\w+), (\w+), )(\d+) */$1.$values{$2}{$3}{$4}/e;
		}

		s/= gcmGETFIELD\(specs, GC_CHIP_SPECS, /= gcmSPEC_FIELD(/g;
	} else {
		if (/gcvFLUSH_/ or /flush = \(Pipe == 0x1\)/) {
			$reg = "AQFlush";
			$sub = "";
		}
		if (/Flush the memory/) {
			$reg = "MMUFlush";
			$sub = "";
		}
		if (/isolat(?:e|ion)|soft reset|frequency scaler/i or /gcvPOWER_ON/) {
			$reg = "GC_CLOCK_CONTROL";
			$sub = "";
		}
		if (/Build control register/ or /Set big endian/) {
			$reg = "GC_CONTROL";
			$sub = "";
		}
		if (/clock gating/) {
			$reg = "GC_PWR_R0";
			$sub = "";
		}
		if (/Disable PE clock/) {
			$reg = "GC_PWR_R1";
			$sub = "";
		}
	}

	s/\( *(gcmSETFIELD\([^)]+\)) *\)/\1/;

	undef $reg if /;/;

	if (defined ($prev)) {
		if (/gcmkONERROR\(gckOS_ReadRegister\(Hardware->os, $hex, &profiler->(\w+)\)\);/) {
			$dbg_elem = $1;
			s/(\s*).*/\1gcmkREAD_DEBUG_REGISTER($dbg_regoff, $dbg_block, $dbg_index, $dbg_elem);/;
			undef $prev;
		} elsif (/gcmkONERROR\(gckOS_WriteRegister\(Hardware->os, $hex, \( *gcmSETFIELD\(0, GC_DEBUG_CONTROL$dbg_regoff, $dbg_block, 0\)/) {
			s/(\s*).*/\1gcmkRESET_DEBUG_REGISTER($dbg_regoff, $dbg_block);/;
			$prev = $_;
			next;
		} elsif (/^\)\)\);$/) {
			$_ = $prev;
			undef $prev;
		}
	} else {
		if (/gcmkONERROR\(gckOS_WriteRegister\(Hardware->os, $hex, gcmSETFIELD\(0, GC_DEBUG_CONTROL(\d), (\w+), (\w+)/) {
			$prev = $_;
			$dbg_regoff = $1;
			$dbg_block = $2;
			$dbg_index = $3;
			next;
		}
	}

	print $prev if defined $prev;
	undef $prev;
	print;

	print STDERR if /(\d+:\d+|####)/;
}
