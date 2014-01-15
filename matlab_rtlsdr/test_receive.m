%%% DUCKY %%%
function test_receive

page_screen_output(0);
page_output_immediately(1);

soc = rtl_sdr_connect;

while(1)

[data, count] = recv(soc, 2);

printf("\nData from last read: %c\n", data);
printf("Count from last read: %d\n", count);
%printf(" -- PAUSED -- ");
%pause

end
