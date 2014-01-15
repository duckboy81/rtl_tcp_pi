%%% DUCKY %%%
function soc = rtl_sdr_setFreqCorr(soc,ppm)

%Send command number first
send_duck(soc, uint8(5));

%Now send parameters
send_duck(soc, uint32(ppm));
