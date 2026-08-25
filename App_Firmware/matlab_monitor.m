%% =====================================================
%  MATLAB - Nhan du lieu tu STM32 qua ESP-01 (TCP)
%  Format: Mode:X,ADC:XXXX,Duty:XX.X,Pos:XX.XX,RPM:XX.XX
%  Tuong thich MATLAB R2020b
%% =====================================================

delete(instrfindall);
clear all;
close all;
clc;

%% --- Cau hinh ---
SERVER_PORT = 8000;
TIMEOUT     = 120;

%% --- Tao TCP Server ---
server = tcpip('0.0.0.0', SERVER_PORT, 'NetworkRole', 'server');
server.Timeout = TIMEOUT;
server.InputBufferSize = 4096;

fprintf('==============================================\n');
fprintf('  MATLAB TCP Server - Port %d\n', SERVER_PORT);
fprintf('  Dang doi ESP-01 ket noi...\n');
fprintf('  (Nhan Ctrl+C de dung)\n');
fprintf('==============================================\n\n');

%% --- Mo server va doi ket noi (blocking) ---
fopen(server);

fprintf('>>> ESP-01 DA KET NOI <<<\n\n');
fprintf('%-6s | %-10s | %-6s | %-12s | %-12s | %-10s\n', ...
    'STT', 'Mode', 'ADC', 'Duty(%)', 'Pos(deg)', 'RPM');
fprintf('%s\n', repmat('-', 1, 70));

%% --- Map mode ---
mode_names = {'FORWARD', 'REVERSE', 'STOP'};

%% --- Vong lap nhan du lieu ---
buffer = '';
count  = 0;
debug_count = 0;

try
    while true
        nb = server.BytesAvailable;
        if nb > 0
            raw = fread(server, nb, 'uint8');
            raw = char(raw');
            
            % Hien thi chuoi GOC (RAW) nhan duoc:
            fprintf('>> RAW: %s\n', strtrim(raw));
            
            buffer = [buffer, raw];

            % Tach tung dong hoan chinh (ket thuc bang \n = char(10))
            while ~isempty(strfind(buffer, char(10)))
                idx  = find(buffer == char(10), 1);
                line = strtrim(buffer(1:idx-1));
                buffer = buffer(idx+1:end);

                if isempty(line)
                    continue;
                end

                % --- Parse du lieu ---
                mode_val = NaN;
                adc_val  = NaN;
                duty_val = NaN;
                pos_val  = NaN;
                rpm_val  = NaN;

                tokens = strsplit(line, ',');
                for i = 1:length(tokens)
                    kv = strsplit(tokens{i}, ':');
                    if length(kv) == 2
                        key = strtrim(kv{1});
                        val = strtrim(kv{2});
                        switch key
                            case 'Mode'
                                mode_val = str2double(val);
                            case 'ADC'
                                adc_val = str2double(val);
                            case 'Duty'
                                duty_val = str2double(val);
                            case 'Pos'
                                pos_val = str2double(val);
                            case 'RPM'
                                rpm_val = str2double(val);
                        end
                    end
                end

                % --- Hien thi ---
                count = count + 1;

                if ~isnan(mode_val) && mode_val >= 0 && mode_val <= 2
                    mode_str = mode_names{mode_val + 1};
                else
                    mode_str = 'UNKNOWN';
                end

                fprintf('%-6d | %-10s | %-6d | %-12.1f | %-12.2f | %-10.2f\n', ...
                    count, mode_str, adc_val, duty_val, pos_val, rpm_val);
            end
        end

        pause(0.05);
    end

catch ME
    fprintf('\n>>> Dung boi: %s\n', ME.message);
end

%% --- Don dep ---
fprintf('\n--- Tong cong nhan duoc: %d ban tin ---\n', count);
fclose(server);
delete(server);
clear server;
fprintf('Da dong server.\n');
