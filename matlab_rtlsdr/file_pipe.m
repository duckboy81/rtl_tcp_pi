function file_pipe()

buffer = 1024;


%fid = popen("rtl_tcp -s 2000000 -f 441000000 -a 0.0.0.0 -n 20", "r");
fid = popen("netcat 127.0.0.1 12345", "r");

	printf("test");
while (ischar (s = fgets (fid)))
  fputs (stdout, s);
endwhile

%for(i = 1:skip)
%    s = fgets(fid);
%if(s == -1)
%        fprintf(stdout, "Problem reading in data");
% return
%end
%end
%
%[d, count] = fscanf(fid, "%d", buffer);
%
%plot(1:buffer, d)
%


return;
