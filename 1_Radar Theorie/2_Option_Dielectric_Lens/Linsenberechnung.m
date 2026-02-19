%         ______   _    ___        __          ___ ____   ____ 
%        |__  / | | |  / \ \      / /         |_ _/ ___| / ___|
%          / /| |_| | / _ \ \ /\ / /   _____   | |\___ \| |    
%         / /_|  _  |/ ___ \ V  V /   |_____|  | | ___) | |___ 
%        /____|_| |_/_/   \_\_/\_/            |___|____/ \____|
% 
%              Zurich University of Applied Sciences
%       Institute of Signal Processing and Wireless Communications
% 
% -------------------------------------------------------------------------
%
% Description:  Beispielskript zur Berechnung einer dielektrischen Linse
% 
%
% Autor:        Pascal Mueller (mupa@zhaw.ch)
% Last change:  09.03.2023
% Version:      1.1

% Formel fuer die Bahn einer hyperbolische Linse
% Friel, Ross J. "3D Printed Radar Lenses with Anti-Reflective Structures",
% MDPI, 11.05.2019
t = @(f,n,r) (f)/(n+1).*(sqrt(1 + ((n+1)./(n-1)) .* (r./f).^2)-1);

% -- Parameter --
% Radius und Fokusabstand sollten so gewählt werden, dass die 3 dB
% Öffnungswinkel der Antenne nicht über den Rand ragen.
% Fokuspunkt ist einige mm hinter den Antennen zu plazieren, da die 
% Antennen keine Punkte sondern Flächen sind.
% Der Abstand zwischen Linse und Antenne sollte > 2...3 lambda sein um die
% Antenne nicht zu stark zu verstimmen.

r = 0:5:50;         % Radius, Pukte vom Zentrum in mm
f = 50;             % Abstand Fokuspunkt vom *Apex* in mm

alpha1 = 45;        % 1. Öffnungswinkel (3 dB)
alpha2 = 60;        % 2. Öffnungswinkel (3 dB)

% Materialwerte unterschiedlicher Materialien und Frequenzen, eps_r (loss)
% -------------------------------------------------------------------------
% Material       24 GHZ         60 GHz
% - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
% PLA            2.55 (0.018)   2.74 (0.014)
% ABS                           2.48
% PE                            2.3 (0.0003)
% PP                            2.2 (0.0005)
% PTFE           2 (0.0001)     2.2 (0.0002)
% PC             2.73 (0.005)
%
% Da viele Werte sehr ungenau sind und das Druckverfahren ebenfalls einen
% Einfluss aufs eps_r hat, eignet sich ein eps_r von 2.5 als Ausgangspunkt.

epsilon_r = 2.5;

% Linsenkurve
y = - t(f, sqrt(epsilon_r), r);

% Linse anzeigen
y = y - min(y);
plot([flip(-r) r],[flip(y) y],'rx-'); hold on;
plot([-r(length(r)) r(length(r))],[0 0],'rx-'); 

% Fokuspunkt darstellen
plot(0, f+max(y),'bo'); 

% Öffnungswinkel darstellen
plot([0, tan(alpha1/2/180*pi)*(f+max(y))], [f+max(y), 0], 'g-'); 
plot([0, -tan(alpha1/2/180*pi)*(f+max(y))], [f+max(y), 0], 'g-');
plot([0, tan(alpha2/2/180*pi)*(f+max(y))], [f+max(y), 0], 'c-'); 
plot([0, -tan(alpha2/2/180*pi)*(f+max(y))], [f+max(y), 0], 'c-'); 

% Distanz von der Linsenfläche zum Fokuspunkt
d_lf = f+max(y);

% Symmetrieachse der Linse
plot([0, 0],[-d_lf*0.1, d_lf*1.1], 'k--')

hold off;

axis equal;
grid on;

ylabel("Distanz zur Linsenfläche [mm]");
xlabel("Distanz zur Mittelachse [mm]");
title("Linse, Fokuspunkt und Öffnungswinkel");
