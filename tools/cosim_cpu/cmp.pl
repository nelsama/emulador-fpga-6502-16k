# bloques separados por la marca F004; quita la primera escritura de los RMW (misma dir. seguida)
sub bl{my($f,$cy)=@_;open F,$f;open G,$cy;my(@r,@w,@c);while(<F>){s/\s+$//;my $t=/T$/;push @w,$_;if($t){push @c,0+<G>};if(/^F004/){my @x;for my $i(0..$#w){next if $i<$#w&&substr($w[$i],0,4)eq substr($w[$i+1],0,4)&&$w[$i]!~/T$/&&$w[$i]!~/^01/;push @x,$w[$i]}push @r,[join('|',@x),$c[0],$c[-1]];@w=();@c=()}}@r}
my @e=bl('emu.txt','emu.cyc');my @v=bl('vhd.txt','vhd.cyc');open O,'ops.txt';chomp(my@o=<O>);my $bad=0;
for my $i(0..$#o){last if $i>$#v||$i>$#e;my $de=$i?$e[$i][1]-$e[$i-1][2]:0;my $dv=$i?$v[$i][1]-$v[$i-1][2]:0;
 if($e[$i][0]ne$v[$i][0]){print "op $o[$i] DATO\n  emu $e[$i][0]\n  vhd $v[$i][0]\n";$bad++} elsif($de!=$dv){print "op $o[$i] CICLOS emu $de vhd $dv\n";$bad++} last if $bad>6}
print "casos: ".scalar(@o)." emu ".@e." vhd ".@v." malos $bad\n";
exit($bad ? 1 : 0);
