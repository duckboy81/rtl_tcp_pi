%
% Hello programming wizard! This matlab file was written
% to ensure data types greater than 8 bits were sent
% properly using the "sockets" package in octave.
%
% Consider the current problem when one needs to send 32bits.
% As of now, the "send" command will only send as much as it
% needs to in order to send what it thinks is all the data.
% If I tried sending uint32(2), I would want 0x0002 to be
% over the wire.  Alas, only 0x2 is sent. This script fixes
% this problem.
%

%Only accepts unsigned values
function duck_result = send_duck(soc, value_to_send)

%Check for input data
if nargin != 2 || !isnumeric(value_to_send)
	duck_result = 0;
	return
end

%Put the value to send in a variable we will destroy later
value_to_send_parts = value_to_send;

%Grab the size of the number
size = whos('value_to_send').bytes;

%Setup array
send_data_array = uint8(0);

%Setup loop to send data one byte at a time
for i = 1:size
	send_data_array(i) = uint8(bitand(value_to_send_parts, 0xFF));
	value_to_send_parts = bitshift(value_to_send_parts, -8);
	
	%Output the value we just saved for debugging purposes
	%printf("Storing value to send array: %i\n", send_data_array(i));
end

%Need to send the data from the front of the array first
%G-Whiz: the `-1` below tells the for-loop to decrement instead of increment
for j = size:-1:1
	%Display message for debugging
	%printf("Sending data to socket: %i\n", send_data_array(j));

	send(soc, send_data_array(j));
end

%Return a value
duck_result = 1;
