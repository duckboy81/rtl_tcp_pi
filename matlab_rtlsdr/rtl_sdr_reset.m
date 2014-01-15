%% DUCKY %%

function soc = rtl_sdr_reset(soc)

printf("Resetting connection!\n")
disconnect(soc);
pause(3);
soc = rtl_sdr_connect;
printf("Connection reset!\n")
