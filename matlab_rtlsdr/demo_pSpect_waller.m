function demo_pSpect

f0 = 96000000;
rate = 2400000;

soc = rtl_sdr_connect
soc = rtl_sdr_setFreq(soc,f0); % set initial frequency to 96Mhz
soc = rtl_sdr_setRate(soc,rate); % set sampling rate to 2.4Mhz

soc = rtl_sdr_setFreqCorr(soc,0); % set frequency correction to 0
soc = rtl_sdr_reset(soc);


%%%%%%%% open figure and create sliders- %%%%%%%%%%%%%%%%%%%%%%%%%%%
figure(1)
        h_fine = uicontrol('Style', 'slider',...
        'Min',-200,'Max',200,'Value',0,...
        'Position', [20 120 400 20],'Callback', @setCorr); drawnow
        
        h_finet = uicontrol('Style', 'text',...
        'Position', [200 90 100 12],'String',sprintf('fine tune: %d',0)); drawnow
         
        h_freqt = uicontrol('Style', 'text',...
        'Position', [200 30 100 12],'String','96'); drawnow
        
        h_freq = uicontrol('Style', 'slider',...
        'Min',0,'Max',1999,'Value',f0/1e6,'SliderStep',[0.0005,0.0005], ...
        'Position', [20 60 400 20],'Callback', @setFreq); drawnow
            
set(h_fine,'UserData',{soc,h_finet})
set(h_freq,'UserData',{soc,h_freqt})
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%


% loop 
while(1)

y = rtl_sdr_getData(soc,2e6); % acuire 2000000 samples
x = reshape(y,2500,800);  % reshape to sequences of length 2500
X = fftshift(fft(x,[],1),1); % compute the spectrum
pSpect = mean(abs(X).^2,2); % compute average power spectrum

freq = str2num(get(h_freqt,'String')); % get center frequency from GUI

% plot log power spectrum
subplot(2,1,1), semilogy(linspace(-1.2,1.2,2500)+freq,pSpect);
axis([-1.2+freq,1.2+freq,1e6,1e10]);

% modify figure to look nice with many ticks
h = gca;
set(h,'XTick',linspace(freq-1,freq+1,21));
drawnow ; % force matlab to display update
end



function setCorr(hObj, event, ax)
% set frequency correction from GUI
    data = get(hObj,'UserData');
    val = floor(get(hObj,'Value'));
    set(data{2},'String',sprintf('fine tune: %d',num2str(val)));
    rtl_sdr_setFreqCorr(data{1},val);

function setFreq(hObj, event, ax)
%  set new center frequency from GUI
    data = get(hObj,'UserData');
    val = floor(get(hObj,'Value'));
    set(data{2},'String',num2str(val*1));
    rtl_sdr_setFreq(data{1},floor((val)*1*1e6));
    

    

