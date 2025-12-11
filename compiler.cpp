#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <linux/elf-em.h>
#include <linux/elf.h>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

#define SECTION_CODE 0
#define SECTION_DATA 0
/*
 * Sources: https://blog.rchapman.org/posts/Linux_System_Call_Table_for_x86_64/
 * https://github.com/torvalds/linux/blob/master/include/uapi/linux/elf.hhttps://github.com/torvalds/linux/blob/master/include/uapi/linux/elf.h
 Regex magic:
 https://stackoverflow.com/questions/7621727/split-a-string-into-words-by-multiple-delimiters
 */

// Load file contents to a string
[[nodiscard]] std::string GetFileContents(const fs::path &filePath) {
  std::ifstream inFile{filePath, std::ios::in | std::ios::binary};
  if (!inFile)
    throw std::runtime_error("Cannot open " + filePath.string());

  const auto fsize = fs::file_size(filePath);
  if (fsize > std::numeric_limits<size_t>::max())
    throw std::runtime_error("file is too large to fit into size_t! " +
                             filePath.string());

  std::string str(static_cast<size_t>(fsize), 0);

  inFile.read(str.data(), str.size());

  if (!inFile)
    throw std::runtime_error("Could not read full contents from " +
                             filePath.string());
  return str;
}
// Source - https://stackoverflow.com/a␍
// Posted by Charles Salvia, modified by community. See post 'Timeline' for
// change history␍ Retrieved 2025-12-11, License - CC BY-SA 4.0␍

bool is_number(const std::string &s) {
  std::string::const_iterator it = s.begin();
  while (it != s.end() && std::isdigit(*it))
    ++it;
  return !s.empty() && it == s.end();
}

std::vector<std::byte> InterpretAsm(std::string &asm_string) {
  std::unordered_map<std::string, std::byte> opcodes{
      {"rax", std::byte{0xB8}}, {"rdi", std::byte{0xBF}},
      {"rsi", std::byte{0xBE}}, {"mov", std::byte{0x48}},
      {"ret", std::byte{0xC3}}, {"nop", std::byte{0x90}}};

  std::byte b_syscall[2]{std::byte{0xF}, std::byte{0x05}};
  std::vector<std::byte> code;

  // split string int words
  // Old: [,\\s]+
  std::regex re(R"("[^"]*"|\[[^\]]*\]|[^,\s]+)");
  std::sregex_token_iterator first{asm_string.begin(), asm_string.end(), re,
                                   -1},
      last;
  std::vector<std::string> words{first, last};

  // Get .code section
  // words from list -> convert to opcodes
  // problem here somewhere
  int curr_section = -1;
  for (auto word : words) {
    // get curr_section
    if (!word.compare(".code")) {
      curr_section = SECTION_CODE;
      continue;
    } else if (!word.compare(".data")) {
      curr_section = SECTION_DATA;
      continue;
    }
    // Translate code section
    if (curr_section == SECTION_CODE) {
      // translate asm instructions, interpret int and string
      if (auto opcode = opcodes.find(word); opcode != opcodes.end()) {
        code.push_back(opcode->second);
      } else if (!word.compare("syscall")) {
        code.push_back(std::byte{0xF});
        code.push_back(std::byte{0x05});
      } else if (is_number(word)) {
        // Could be nunber

        /// string to 64 bit little endian number
        uint64_t num = std::stoull(word);
        for (size_t i = 0; i < 8; i++) {
          code.push_back(
              std::byte{static_cast<uint8_t>((num >> (8 * i)) & 0xFF)});
        }
      } else {
        // Includes the null terminator
        if (word[0] != '"') {
          throw std::runtime_error(
              "Strings must start and end with double quotes!\n");
        }
        for (int i = 0; i < word.length(); i++) {
          // Untested !!!
          if (word[i] == '"')
            continue;
          code.push_back(static_cast<std::byte>(word[i]));
        }
        code.push_back(static_cast<std::byte>('\0'));
      }
    }
    // Translate data section
    else if (curr_section == SECTION_DATA) {
    }
  }
  return code;
}

Elf64_Ehdr MakeElfHeader(const uint64_t &baseaddr) {
  Elf64_Ehdr elf_header = {0};
  elf_header.e_ident[EI_MAG0] = 0x7f;
  elf_header.e_ident[EI_MAG1] = 'E';
  elf_header.e_ident[EI_MAG2] = 'L';
  elf_header.e_ident[EI_MAG3] = 'F';

  elf_header.e_ident[EI_CLASS] = 2;
  elf_header.e_ident[EI_DATA] = ELFDATA2LSB; // little endian
  elf_header.e_ident[EI_VERSION] = EV_CURRENT;
  elf_header.e_ident[EI_OSABI] = 0;

  elf_header.e_type = ET_EXEC;
  elf_header.e_machine = EM_X86_64;
  elf_header.e_version = EV_CURRENT;

  elf_header.e_ehsize = sizeof(Elf64_Ehdr);
  elf_header.e_phentsize = sizeof(Elf64_Phdr);
  elf_header.e_phnum = 1;

  elf_header.e_phoff = sizeof(Elf64_Ehdr);
  elf_header.e_entry = baseaddr + sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
  return elf_header;
}

Elf64_Phdr MakePHeader(const uint64_t &baseaddr, size_t codesz) {
  Elf64_Phdr program_header = {0};
  program_header.p_type = PT_LOAD;
  program_header.p_flags = PF_R | PF_X;
  program_header.p_offset = 0;
  program_header.p_vaddr = baseaddr;
  program_header.p_paddr = baseaddr;
  program_header.p_filesz = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + codesz;
  program_header.p_memsz = program_header.p_filesz;
  program_header.p_align = 0x1000;
  return program_header;
}

int main(int argc, char *argv[]) {
  if (argc < 2)
    throw std::runtime_error("Usage: <path>\n");

  // Read assembly to a string
  std::string assembly_contents = GetFileContents(argv[1]);

  // generate code section
  std::vector<std::byte> opcode = InterpretAsm(assembly_contents);
  if (opcode.size() <= 0)
    throw std::runtime_error("Couldnt generate code!\n");

  // create array from opcode vector
  uint8_t code[opcode.size()];
  for (int i = 0; i < opcode.size(); i++) {
    code[i] = std::to_integer<uint8_t>(opcode[i]);
  }

  // Create x64 elf file header
  const uint64_t baseaddr = 0x400000;
  Elf64_Ehdr elf_header = MakeElfHeader(baseaddr);
  Elf64_Phdr program_header = MakePHeader(baseaddr, sizeof(code));

  // check file_size
  std::cout << sizeof(Elf64_Ehdr) << ", should be 64b\n";
  std::cout << sizeof(Elf64_Phdr) << ", should be 56b\n";

  // Write to disk
  std::ofstream out("my_program", std::ios::binary);
  out.write((char *)&elf_header, sizeof(elf_header));
  out.write((char *)&program_header, sizeof(program_header));
  out.write((char *)code, sizeof(code)); // .code segment
  return 0;
}
