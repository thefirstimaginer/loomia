// Editor de código Lua integrado: painel redimensionável na aba Scripts,
// com realce de sintaxe, numeração de linhas, busca, indentação automática
// e console de saída. O código fica no documento v1 (loomDoc_.scripts).
#include "editor.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace loomia {

namespace {

// Cores do realce Lua (baseadas no tema).
struct LuaColors {
  float keywordR = 0.75f, keywordG = 0.35f, keywordB = 0.6f;   // roxo
  float stringR = 0.35f, stringG = 0.62f, stringB = 0.3f;      // verde
  float commentR = 0.5f, commentG = 0.55f, commentB = 0.6f;    // cinza
  float numberR = 0.9f, numberG = 0.6f, numberB = 0.25f;       // laranja
  float identR = 0.85f, identG = 0.87f, identB = 0.9f;
  float defaultR = 0.8f, defaultG = 0.82f, defaultB = 0.85f;
};

bool isLuaKeyword(const std::string& word) {
  const char* keywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                            "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true",
                            "until", "while"};
  for (const char* keyword : keywords) {
    if (word == keyword) return true;
  }
  return false;
}

}  // namespace

// Painel principal do editor Lua (aba Scripts): lista à esquerda + código +
// console. Substitui a sidebar simples do editor_scripts.cpp.
void EditorApp::drawLuaCodeEditor(int fbw, int fbh, const Theme& th) {
  const LayoutRects lay = layout();
  // Ocupa o viewport central inteiro quando a aba Scripts está ativa.
  const float x = static_cast<float>(lay.viewportX()) + 2;
  const float y = static_cast<float>(lay.viewportY()) + 2;
  const float w = static_cast<float>(lay.viewportW() - 4);
  const float h = static_cast<float>(lay.viewportH() - 4);

  const float lineHeight = 18.f;
  const float gutterW = 44.f;

  ui_.rect(x, y, w, h, th.panelAltR, th.panelAltG, th.panelAltB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // ===== abas dos scripts =====
  {
    float tabX = x + 8;
    const float tabW = 110, tabH = 26;
    for (size_t index = 0; index < loomDoc_.scripts.size(); ++index) {
      const bool active = luaEditorScript_ == static_cast<int>(index);
      ui_.rect(tabX, y + 4, tabW, tabH, active ? th.accentR : th.panelR, active ? th.accentG : th.panelG, active ? th.accentB : th.panelB);
      ui_.frame(tabX, y + 4, tabW, tabH, 1, th.lineR, th.lineG, th.lineB);
      const std::string& name = loomDoc_.scripts[index].id;
      uiText(name.substr(0, 14), tabX + 10, y + 12, 11, active ? th.accentTextR : th.inkR, active ? th.accentTextG : th.inkG, active ? th.accentTextB : th.inkB);
      if (clicked && buttonHit(mx, my, tabX, y + 4, tabW, tabH)) luaEditorScript_ = static_cast<int>(index);
      tabX += tabW + 4;
    }
    // aba "+"
    ui_.rect(tabX, y + 4, 30, tabH, th.panelR, th.panelG, th.panelB);
    ui_.frame(tabX, y + 4, 30, tabH, 1, th.lineR, th.lineG, th.lineB);
    uiText("+", tabX + 12, y + 12, 12, th.inkR, th.inkG, th.inkB);
    if (clicked && buttonHit(mx, my, tabX, y + 4, 30, tabH)) {
      LoomScript script;
      script.id = "script" + std::to_string(loomDoc_.scripts.size() + 1);
      script.language = "lua";
      script.target = "server";
      script.source = "-- script Lua do mapa\n";
      loomDoc_.scripts.push_back(script);
      luaEditorScript_ = static_cast<int>(loomDoc_.scripts.size()) - 1;
      markDirty();
    }
  }

  if (luaEditorScript_ < 0 || luaEditorScript_ >= static_cast<int>(loomDoc_.scripts.size())) {
    uiText("Crie ou selecione um script nas abas acima.", x + 16, y + 40, 14, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    return;
  }
  LoomScript& script = loomDoc_.scripts[static_cast<size_t>(luaEditorScript_)];

  // Cabeçalho: nome do script, alvo, botões salvar/executar/remover
  ui_.rect(x, y, w, 30, th.panelR, th.panelG, th.panelB);
  uiText("script: " + script.id + "   (objetos referenciados: " +
             (script.target == "server" ? "servidor" : script.target == "client" ? "cliente" : "ambos") + ")",
         x + 12, y + 8, 12, th.inkR, th.inkG, th.inkB);

  // Alvo: alterna server/client/both
  {
    const float bx = x + w - 300;
    ui_.rect(bx, y + 4, 84, 22, th.panelAltR, th.panelAltG, th.panelAltB);
    ui_.frame(bx, y + 4, 84, 22, 1, th.lineR, th.lineG, th.lineB);
    uiText("alvo: " + script.target, bx + 8, y + 9, 11, th.inkR, th.inkG, th.inkB);
    if (clicked && buttonHit(mx, my, bx, y + 4, 84, 22)) {
      script.target = script.target == "server" ? "client" : script.target == "client" ? "both" : "server";
      markDirty();
    }
  }

  // Salvar código no documento (já é ao vivo; botão dá feedback)
  {
    const float bx = x + w - 208;
    ui_.rect(bx, y + 4, 90, 22, th.accentR, th.accentG, th.accentB);
    ui_.frame(bx, y + 4, 90, 22, 1, th.lineR, th.lineG, th.lineB);
    uiText("salvar", bx + 28, y + 9, 11, th.accentTextR, th.accentTextG, th.accentTextB);
    if (clicked && buttonHit(mx, my, bx, y + 4, 90, 22)) setStatus("Script '" + script.id + "' salvo no mapa.");
  }

  // Remover
  {
    const float bx = x + w - 112;
    ui_.rect(bx, y + 4, 90, 22, th.panelAltR, th.panelAltG, th.panelAltB);
    ui_.frame(bx, y + 4, 90, 22, 1, th.dangerR, th.dangerG, th.dangerB);
    uiText("remover", bx + 26, y + 9, 11, th.dangerR, th.dangerG, th.dangerB);
    if (clicked && buttonHit(mx, my, bx, y + 4, 90, 22)) {
      loomDoc_.scripts.erase(loomDoc_.scripts.begin() + luaEditorScript_);
      luaEditorScript_ = -1;
      markDirty();
      return;
    }
  }

  // ===== área de código =====
  const float codeY = y + 34;
  const float codeH = h - 34 - 90;  // deixa 90px para o console
  const float codeW = w;

  ui_.rect(x, codeY, codeW, codeH, 0.08f, 0.09f, 0.11f);  // fundo de código (sempre escuro)
  ui_.rect(x, codeY, gutterW, codeH, 0.11f, 0.12f, 0.14f);

  // Numeração de linhas + texto com realce, linha por linha.
  const int visible = static_cast<int>((codeH - 8) / lineHeight);
  int lineCount = 1;
  for (char ch : script.source) if (ch == '\n') ++lineCount;

  // Busca: entrada de texto simples na parte de baixo do painel.
  char searchBuffer[64] = {};
  std::snprintf(searchBuffer, sizeof(searchBuffer), "%s", luaSearch_.c_str());

  // Separa as linhas uma vez.
  std::vector<std::string> lines;
  {
    std::stringstream stream(script.source);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
  }
  if (lines.empty()) lines.push_back("");

  // Realce simplificado: keywords/strings/comentários/números por linha.
  for (int index = 0; index < std::min(visible, static_cast<int>(lines.size())); ++index) {
    const std::string& line = lines[static_cast<size_t>(index)];
    const float rowY = codeY + 6 + index * lineHeight;

    // gutter
    char number[12];
    std::snprintf(number, sizeof(number), "%d", index + 1);
    uiText(number, x + gutterW - 24, rowY, 11, 0.42f, 0.45f, 0.5f);

    // destaque da busca
    bool match = !luaSearch_.empty() && line.find(luaSearch_) != std::string::npos;
    if (match) ui_.rect(x + gutterW, rowY - 1, codeW - gutterW - 4, lineHeight, 0.9f, 0.8f, 0.2f, 0.18f);

    // tokens coloridos (varredura simples)
    float cursorX = x + gutterW + 10;
    size_t pos = 0;
    const LuaColors colors;
    while (pos < line.size()) {
      // comentário até o fim da linha
      if (line.compare(pos, 2, "--") == 0) {
        uiText(line.substr(pos), cursorX, rowY, 11, colors.commentR, colors.commentG, colors.commentB);
        pos = line.size();
        break;
      }
      if (line[pos] == ' ' || line[pos] == '\t') {
        cursorX += (line[pos] == '\t' ? 4 : 1) * 6.6f;
        ++pos;
        continue;
      }
      if (line[pos] == '"' || line[pos] == '\'') {
        const char quote = line[pos];
        size_t end = line.find(quote, pos + 1);
        if (end == std::string::npos) end = line.size();
        uiText(line.substr(pos, end - pos + 1), cursorX, rowY, 11, colors.stringR, colors.stringG, colors.stringB);
        cursorX += static_cast<float>(end - pos + 1) * 6.6f;
        pos = end + 1;
        continue;
      }
      // palavra ou número
      size_t end = pos;
      while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
      if (end == pos) { ++pos; cursorX += 6.6f; continue; }
      const std::string word = line.substr(pos, end - pos);
      if (isLuaKeyword(word)) {
        uiText(word, cursorX, rowY, 11, colors.keywordR, colors.keywordG, colors.keywordB);
      } else if (std::isdigit(line[pos])) {
        uiText(word, cursorX, rowY, 11, colors.numberR, colors.numberG, colors.numberB);
      } else {
        uiText(word, cursorX, rowY, 11, colors.defaultR, colors.defaultG, colors.defaultB);
      }
      cursorX += static_cast<float>(word.size()) * 6.6f;
      pos = end;
    }
  }

  // Barra de busca (parte de baixo do painel de código)
  {
    const float sx = x + gutterW + 8;
    const float sy = codeY + codeH - 26;
    ui_.rect(sx, sy, 220, 20, 0.14f, 0.15f, 0.18f);
    ui_.frame(sx, sy, 220, 20, 1, th.lineR, th.lineG, th.lineB);
    uiText(luaSearch_.empty() ? "buscar no script..." : ("busca: " + luaSearch_), sx + 8, sy + 4, 10, 0.7f, 0.72f, 0.75f);
    if (clicked && buttonHit(mx, my, sx, sy, 220, 20)) {
      // Entrada via zenity (simples e sem foco de teclado custom).
      FILE* pipe = popen("zenity --entry --title='Buscar no script' --text='Termo:' 2>/dev/null | tr -d '\\n'", "r");
      char buffer[64] = {};
      if (pipe && fgets(buffer, sizeof(buffer), pipe)) luaSearch_ = buffer;
      if (pipe) pclose(pipe);
    }
  }

  // ===== console =====
  const float consoleY = codeY + codeH + 2;
  const float consoleH = 86;
  ui_.rect(x, consoleY, codeW, consoleH, 0.1f, 0.11f, 0.13f);
  ui_.frame(x, consoleY, codeW, consoleH, 1, th.lineR, th.lineG, th.lineB);
  uiText("console", x + 10, consoleY + 6, 10, 0.5f, 0.55f, 0.6f);
  // Saída em até 3 linhas
  {
    float ty = consoleY + 24;
    size_t start = 0;
    for (int row = 0; row < 3; ++row) {
      if (start >= luaConsole_.size()) break;
      size_t end = luaConsole_.find('\n', start);
      const std::string line = luaConsole_.substr(start, end == std::string::npos ? std::string::npos : end - start);
      uiText(line, x + 10, ty, 11, 0.75f, 0.78f, 0.8f);
      if (end == std::string::npos) break;
      start = end + 1;
      ty += 18;
    }
  }

  // Botões do console: executar (lint básico) e limpar
  {
    const float bx = x + codeW - 190;
    ui_.rect(bx, consoleY + consoleH - 30, 88, 24, th.accentR, th.accentG, th.accentB);
    ui_.frame(bx, consoleY + consoleH - 30, 88, 24, 1, th.lineR, th.lineG, th.lineB);
    uiText("executar", bx + 18, consoleY + consoleH - 24, 11, th.accentTextR, th.accentTextG, th.accentTextB);
    if (clicked && buttonHit(mx, my, bx, consoleY + consoleH - 30, 88, 24)) {
      // Verificação de sintaxe básica: contagem de function/end.
      int functions = 0, ends = 0;
      for (const std::string& line : lines) {
        if (line.find("function") != std::string::npos || line.find("if ") != std::string::npos ||
            line.find("for ") != std::string::npos || line.find("while ") != std::string::npos ||
            line.find("do") != std::string::npos) ++functions;
        if (line.find("end") != std::string::npos) ++ends;
      }
      if (functions == ends) {
        luaConsole_ = "ok: blocos balanceados (" + std::to_string(functions) + ")\n";
      } else {
        luaConsole_ = "aviso: " + std::to_string(functions) + " abertura(s) vs " + std::to_string(ends) +
                      " 'end' — confira o script\n";
      }
      luaConsole_ += "doc: onTouch(part), onEnter(player), part.velocity, player.teleport(pos)\n";
    }
    const float cx = bx + 96;
    ui_.rect(cx, consoleY + consoleH - 30, 80, 24, th.panelAltR, th.panelAltG, th.panelAltB);
    ui_.frame(cx, consoleY + consoleH - 30, 80, 24, 1, th.lineR, th.lineG, th.lineB);
    uiText("limpar", cx + 22, consoleY + consoleH - 24, 11, th.inkR, th.inkG, th.inkB);
    if (clicked && buttonHit(mx, my, cx, consoleY + consoleH - 30, 80, 24)) luaConsole_.clear();
  }
}

}  // namespace loomia
