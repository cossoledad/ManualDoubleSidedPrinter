# MDSPrinter

���� HP LaserJet Pro MFP M126a ���ֶ�˫���ӡ���ߡ�Windows �����ʹ�� C++20��Win32��Conan 2 �� CMake��Web �沿�������Ӵ�ӡ���� Ubuntu ARM64 �������ֻ��͵��Կ�ͨ����������������ϴ���Ԥ���ʹ�ӡ PDF��

GitHub Release �ṩ Windows ��װ������Я ZIP �� Ubuntu ARM64 Web ����Web ��Ĭ�϶˿�Ϊ `30128`���� [����˵��](web/README.md)��

## ʹ��

1. ѡ�� PDF �ʹ�ӡĿ�ꡣҳ�뷶Χ���մ�ӡȫ����Ҳ������ `1-3,5,8-10`���Զ���ҳ�밴�ĵ�˳�����в�ȥ�ء�
2. ���Ҳ�Ԥ�����л� **ԭ��**��**��һ��** �� **�ڶ���**��������ָ��Ϊ�������ţ���ס�����������������ű����������϶���˫��Ԥ������ **�ʺϴ���** �ָ���ҳ��ʾ��
3. ��� **��ӡ��һ��**���ȴ�ӡ��ȫ����ֽ��˫��������Զ��е��ڶ���Ԥ�������Żط�ʾ�⡣
4. ������ֽ˳ʱ����ת 180�㣬����ӡˢ�泯�򣬷Żؽ�ֽ�̡���� **�ѷŻأ���ӡ�ڶ���**��

�ط�ʾ���ѭ�����ţ�ֱ�������������ǰ���񡣹ر� Windows �Ķ���Ч���󣬽�����ʾ��תǰ��ľ�̬ʾ�⡣

�� **��ӡ��** ������ѡ�� **ģ���ӡ������ֽ��**������������һ�顢�Ż�ֽ�������͵ڶ��飬�������Ӵ�ӡ����

ѡ�е�ҳʱֻ�ύһ������ѡ����ҳʱ����һ��˳��Ϊ `1, 3, 5`���ڶ���Ϊ `�հ�, 4, 2`���հ�ҳ�õڶ������͹����һ��ֽ����ӡ�������������������հ�ҳ�����ӡ������ Windows ��ӡ��ѡ���йر���Щѡ�

## �����ͷ���

ʹ�� Windows 10 22H2 �� Windows 11�������� C++ �������غ� Windows SDK �� Visual Studio 2022 �� 2026��CMake 3.24 ���ϣ�VS 2026 �� Visual Studio ��������Ҫ CMake 4.2 ���ϣ���Python �� Conan 2.28 ���ϡ�

```powershell
python -m pip install 'conan>=2.28,<3'
conan profile detect
conan build . -of build/conan -s:h build_type=Release -s:h arch=x86_64 -s:h compiler.cppstd=20 -s:h compiler.runtime=static
ctest --test-dir build/conan/build -C Release --output-on-failure
./build/conan/build/Release/ManualDoubleSidedPrinter.exe
```

Conan �ṩ CMake ��������Ӧ�ô���䷽����ǰ������ Windows SDK �ṩ����װ Inno Setup 6 ���� PowerShell 7 ���� `./scripts/package-installer.ps1`������ `artifacts/installer` ���ɰ�װ���ͱ�Я ZIP�����͵� `main` ʱ��GitHub Actions ʹ��ͬһ�ű����������Բ�������

����ʱ���������ú� MSVC �� Ninja �Ŀ������ն���ֱ�ӹ�����

```powershell
cmake -S . -B build/ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ninja
ctest --test-dir build/ninja --output-on-failure
```

## VS Code ����

�� Windows �� VS Code �д򿪲ֿ�Ŀ¼����װ Microsoft **C/C++** ��չ��`ms-vscode.cpptools`������ **F5** ��ѡ�� **MDSPrinter (Debug)**������������Զ����� MSVC x64 ����������ʹ�� PATH �ϵ� Ninja��Ҳ֧�� Visual Studio ������ Ninja������ `src/main.cpp` �� `wWinMain` ���������ϵ㣬���� **Ctrl+Shift+B** ����������

Debug ��� PDB λ�� `build/vscode-debug`���������ص���·�����������С�

## PDF ��Ⱦ

Ԥ������ϵͳ PDF ��Ⱦ�ӿڣ��ں�̨��ҳ��Ⱦ����ӡ���ɴ�ӡ����ȱ���С�����У�λͼ��� 300 DPI��������� 4096 ���ء�
