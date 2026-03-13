Name:       tizenclaw-assets
Summary:    TizenClaw ML/AI Assets — ONNX Runtime, RAG, OCR
Version:    1.0.0
Release:    1
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz

BuildRequires: cmake

%description
Consolidated ML/AI asset package for TizenClaw.
Contains ONNX Runtime library, RAG knowledge databases, embedding model,
and PaddleOCR PP-OCRv3 on-device OCR engine with CLI tool.

# OCR model: "lite" (Korean+English, ~13MB) or "full" (CJK, ~84MB)
%if "%{?ocr_model}" == "full"
  %define _ocr_model full
%else
  %define _ocr_model lite
%endif

%prep
%setup -q
cp packaging/%{name}.manifest .

%build
cmake -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      -DTIZENCLAW_ARCH=%{_arch} \
      -DOCR_MODEL=%{_ocr_model} \
      -DORT_INCLUDE_DIR=%{_builddir}/%{name}-%{version}/include \
      .
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%make_install

mkdir -p %{buildroot}/opt/usr/share/tizenclaw/rag
mkdir -p %{buildroot}/opt/usr/share/tizenclaw/lib
mkdir -p %{buildroot}/opt/usr/share/tizenclaw/models
mkdir -p %{buildroot}/opt/usr/share/tizenclaw/tools/cli/tizenclaw-ocr

%files
%defattr(-,root,root,-)
%manifest %{name}.manifest
/opt/usr/share/tizenclaw/rag/
/opt/usr/share/tizenclaw/lib/
/opt/usr/share/tizenclaw/models/
/opt/usr/share/tizenclaw/tools/cli/tizenclaw-ocr/
