%%% DUCKY %%%
function data = rtl_sdr_getData_notext(soc,samps)
% data = rtl_sdr_getData(soc, samps)
%
% soc: tcp socket (from rtl_sdr_connect)
% sams: number of samples (default 1e6)
%	[This should be equal to your sample rate]
%


totBytes = samps*2;

%printf("Receiving %d bytes!!\n", totBytes);
[data, count] = recv(soc, totBytes, MSG_WAITALL);
%printf("Only got %d bytes\n", count);

%printf("I count this many: %d\n", numel(data));

%printf("Converting to double\n");
data = double(data);

% This below line may be huge!!
%printf(data);

%data = double(data_reader.readBuffer(totBytes));

% Java does not support unsigned byte, but the data
% is unsigned, so fix it! Any other suggestions welcome!
%data = data-128*(data>0) + 128*(data<0);

%printf("converting to complex\n");
% convert from real, imag to complex
data = data(1:2:end) + 1i*data(2:2:end);




