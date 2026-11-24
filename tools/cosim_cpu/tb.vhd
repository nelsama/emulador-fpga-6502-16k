library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;use std.textio.all;
entity tb is end;architecture a of tb is
 type ram is array(0 to 65535) of unsigned(7 downto 0);
 signal clk:std_logic:='0';signal rst:std_logic:='1';signal di,do:unsigned(7 downto 0):=x"00";signal ad:unsigned(15 downto 0);signal we:std_logic;signal fin:boolean:=false;
 begin
 u:entity work.cpu65xx(fast) generic map(false,false,false) port map(clk=>clk,enable=>'1',reset=>rst,nmi_n=>'1',irq_n=>'1',di=>di,do=>do,addr=>ad,we=>we);
 process begin for i in 1 to 10 loop clk<='0';wait for 5 ns;clk<='1';wait for 5 ns;end loop;rst<='0';
  while not fin loop clk<='0';wait for 5 ns;clk<='1';wait for 5 ns;end loop;wait;end process;
 process
  variable m:ram;file f:text;file g:text;file q:text;variable l:line;variable i:integer;variable c:integer:=0;
  begin file_open(f,"mem.hex",read_mode);for k in 0 to 65535 loop readline(f,l);read(l,i);m(k):=to_unsigned(i,8);end loop;
  file_open(g,"vhd.txt",write_mode);file_open(q,"vhd.cyc",write_mode);
  loop wait until falling_edge(clk);
   if rst='0' then c:=c+1;
    if we='1' then m(to_integer(ad)):=do; hwrite(l,std_logic_vector(ad));write(l,' ');hwrite(l,std_logic_vector(do));if ad(15 downto 8)=x"F0" then write(l,string'(" T"));end if;writeline(g,l);
     if ad(15 downto 8)=x"F0" then write(l,c);writeline(q,l);end if;
     if ad=x"F0FF" then fin<=true;file_close(g);file_close(q);wait;end if;end if;end if;
   if not is_x(std_logic_vector(ad)) then di<=m(to_integer(ad));end if;
   if c>300000 then fin<=true;file_close(g);file_close(q);wait;end if;
  end loop;end process;end;
