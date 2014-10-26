library ieee;
use ieee.std_logic_1164.all;

entity debouncer is
  generic
  (
    RESET_VALUE     : std_logic := '1';
    DEBOUNCE_CYCLES : integer := (100 ms)/(20 ns)
  );
  port
  (
    clk     : in std_logic;
    rst_n   : in std_logic;
    
    btn_in  : in std_logic;
    btn_out : out std_logic
  );
end entity debouncer;