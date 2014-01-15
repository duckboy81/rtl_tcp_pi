%%% DUCKY %%%
function soc = rtl_sdr_setFreq(soc,freq)

%Send command number first
send_duck(soc, uint8(1));

%Now send paramaters
send_duck(soc, uint32(freq));
