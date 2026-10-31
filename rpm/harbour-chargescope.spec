Name:       harbour-chargescope
# Builds take their version from the git tag vX.Y.Z: CI rewrites this line, and sfdk uses the
# newest tag. Set it to the release in the commit you tag (see the README).
Version:    0.1.0
Release:    1
Summary:    What a charger offers and how the phone uses it
License:    MIT
URL:        https://github.com/ian-te/chargescope
Source0:    %{name}-%{version}.tar.bz2
Requires:   sailfishsilica-qt5 >= 0.10.9
BuildRequires: pkgconfig(sailfishapp) >= 1.0.2
BuildRequires: pkgconfig(Qt5Core)
BuildRequires: pkgconfig(Qt5Qml)
BuildRequires: pkgconfig(Qt5Quick)
BuildRequires: desktop-file-utils

%description
Shows the live power going into the battery and whether the phone is direct
charging (PPS), on USB PD or on basic USB power, the source PDOs the charger
offers and which one is in use, and what the phone's charger chips report.
Made for the Jolla Phone (MediaTek, MT6379 charger).

# Metadata for SailfishOS:Chum, see
# https://github.com/sailfishos-chum/main/blob/main/Metadata.md
%if 0%{?_chum}
Title: ChargeScope
Type: desktop-application
DeveloperName: Ian Te
Categories:
 - Utility
 - System
Custom:
  Repo: https://github.com/ian-te/chargescope
PackageIcon: https://github.com/ian-te/chargescope/raw/main/icons/harbour-chargescope.svg
AIRating: V
AINote: Written almost entirely by Claude (Anthropic)
Links:
  Homepage: https://github.com/ian-te/chargescope
  Bugtracker: https://github.com/ian-te/chargescope/issues
%endif

%prep
%autosetup

%build
%qmake5 VERSION=%{version}
%make_build

%install
%qmake5_install
desktop-file-install --delete-original --dir %{buildroot}%{_datadir}/applications %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
