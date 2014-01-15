%%% DUCKY %%%
function soc = rtl_sdr_setRate(soc,rate)

%Send command number first
send_duck(soc, uint8(2));

%Now send parameters
send_duck(soc, uint32(rate));
