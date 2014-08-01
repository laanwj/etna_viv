for X in  *.[c]; do
    mv $X $X.bak
    #perl /home/orion/projects/etna_viv/tools/deobfuscate-complex-vivante.pl $X.bak > $X
    perl /home/orion/projects/etna_viv/tools/deobfuscate-simple2-vivante.pl $X.bak > $X
done

