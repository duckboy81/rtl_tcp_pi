%function waller1(f0, port)
function ducky1(f0)

page_screen_output(0);
page_output_immediately(1);

f0 = f0 * 10^6
rate = 2000;
%rate = 2200000;
%N = 100;
N = 2e6;

soc = rtl_sdr_connect("127.0.0.1", 1234);
%send_soc = rtl_sdr_connect_port(port);

soc = rtl_sdr_setFreq(soc,f0); % set initial frequency
soc = rtl_sdr_setRate(soc,rate); % set sampling rate

soc = rtl_sdr_setFreqCorr(soc,0); % set frequency correction to 0

% Resetting the connection may cause undesirable behavior
% when RTL_TCP runs on an ARM chip
%soc = rtl_sdr_reset(soc);

%%%%%%%%% open figure and create sliders- %%%%%%%%%%%%%%%%%%%%%%%%%%%
%figure(1)
%        h_fine = uicontrol('Style', 'slider',...
%        'Min',-200,'Max',200,'Value',0,...
%        'Position', [20 120 400 20],'Callback', @setCorr); drawnow
%        
%        h_finet = uicontrol('Style', 'text',...
%        'Position', [200 90 100 12],'String',sprintf('fine tune: %d',0)); drawnow
%         
%        h_freqt = uicontrol('Style', 'text',...
%        'Position', [200 30 100 12],'String','96'); drawnow
%        
%        h_freq = uicontrol('Style', 'slider',...
%        'Min',0,'Max',1999,'Value',f0/1e6,'SliderStep',[0.0005,0.0005], ...
%        'Position', [20 60 400 20],'Callback', @setFreq); drawnow
%            
%set(h_fine,'UserData',{soc,h_finet})
%set(h_freq,'UserData',{soc,h_freqt})
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

%while(1)
%
%printf("\n\n")
%y = rtl_sdr_getData(soc, N); % acuire one capture
%
%printf("Sending %d elements", numel(y));
%send(send_soc, y);
%
%end

while(1)

time_start_loop = time;

printf("Acquiring data from RTL_TCP\n");
y = rtl_sdr_getData(soc, N); % acuire one capture

time_start_translation = time;

yLen = numel(y)

NFFT = 2^nextpow2(yLen);
Y = fft(y, NFFT)/yLen;
time_end_translation = time;

f = f0 / 2 * linspace(0,1,21);
plot(f, 2*abs(Y(1:NFFT/2+1)));
title("Ducky's FFT from MATHWORKS.com")
xlabel("Freq (Hz)");
ylabel("|Y(f)|");

printf("Now: drawnow\n");
drawnow

printf("End of loop, cycling\n");
time_end_loop = time;

time_spent_translation = double(time_end_translation - time_start_translation);
time_spent_loop = double(time_end_loop - time_start_loop);
time_spent_fraction = double(time_spent_translation / time_spent_loop);

printf("Time spent on fft: %dms\n", time_spent_translation*1000);
printf("Time spent on other things: %dms\n", (time_spent_loop - time_spent_translation)*1000); 
printf("Fraction of time spent crunching numbers: %f\n", time_spent_fraction);
end
