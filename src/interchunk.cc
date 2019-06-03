#include <interchunk.h>
#include <apertium/trx_reader.h>
#include <apertium/utf_converter.h>
#include <lttoolbox/compression.h>
#include <lttoolbox/xml_parse_util.h>

#include <cctype>
#include <cerrno>
#include <iostream>
#include <stack>
#include <apertium/string_utils.h>
//#include <apertium/unlocked_cstdio.h>

using namespace Apertium;
using namespace std;

Interchunk::Interchunk()
{
  furtherInput = true;
  allDone = false;
  recursing = false;
}

Interchunk::~Interchunk()
{
}

void
Interchunk::readData(FILE *in)
{
  alphabet.read(in);
  any_char = alphabet(TRXReader::ANY_CHAR);
  any_tag = alphabet(TRXReader::ANY_TAG);

  Transducer t;
  t.read(in, alphabet.size());

  map<int, int> finals;

  // finals
  for(int i = 0, limit = Compression::multibyte_read(in); i != limit; i++)
  {
    int key = Compression::multibyte_read(in);
    finals[key] = Compression::multibyte_read(in);
  }

  me = new MatchExe(t, finals);

  // attr_items
  bool recompile_attrs = Compression::string_read(in) != string(pcre_version());
  for(int i = 0, limit = Compression::multibyte_read(in); i != limit; i++)
  {
    wstring const cad_k = Compression::wstring_read(in);
    attr_items[cad_k].read(in);
    wstring fallback = Compression::wstring_read(in);
    if(recompile_attrs) {
      attr_items[cad_k].compile(UtfConverter::toUtf8(fallback));
    }
  }

  // variables
  for(int i = 0, limit = Compression::multibyte_read(in); i != limit; i++)
  {
    wstring const cad_k = Compression::wstring_read(in);
    variables[cad_k] = Compression::wstring_read(in);
  }

  // macros
  for(int i = 0, limit = Compression::multibyte_read(in); i != limit; i++)
  {
    wstring const cad_k = Compression::wstring_read(in);
    macros[cad_k] = Compression::multibyte_read(in);
  }

  // lists
  for(int i = 0, limit = Compression::multibyte_read(in); i != limit; i++)
  {
    wstring const cad_k = Compression::wstring_read(in);

    for(int j = 0, limit2 = Compression::multibyte_read(in); j != limit2; j++)
    {
      wstring const cad_v = Compression::wstring_read(in);
      lists[cad_k].insert(cad_v);
      listslow[cad_k].insert(StringUtils::tolower(cad_v));
    }
  }
}

void
Interchunk::read(string const &transferfile, string const &datafile)
{
  // rules
  FILE *in = fopen(transferfile.c_str(), "rb");
  longestPattern = fgetwc(in);
  int count = fgetwc(in);
  int len;
  wstring cur;
  for(int i = 0; i < count; i++)
  {
    cur.clear();
    fgetwc(in);
    len = fgetwc(in);
    for(int j = 0; j < len; j++)
    {
      cur.append(1, fgetwc(in));
    }
    rule_map.push_back(cur);
  }
  fclose(in);

  // datafile
  FILE *datain = fopen(datafile.c_str(), "rb");
  if(!datain)
  {
    wcerr << "Error: Could not open file '" << datafile << "'." << endl;
    exit(EXIT_FAILURE);
  }
  readData(datain);
  fclose(datain);
}

bool
Interchunk::beginsWith(wstring const &s1, wstring const &s2) const
{
  int const limit = s2.size(), constraint = s1.size();

  if(constraint < limit)
  {
    return false;
  }
  for(int i = 0; i != limit; i++)
  {
    if(s1[i] != s2[i])
    {
      return false;
    }
  }

  return true;
}

bool
Interchunk::endsWith(wstring const &s1, wstring const &s2) const
{
  int const limit = s2.size(), constraint = s1.size();

  if(constraint < limit)
  {
    return false;
  }
  for(int i = limit-1, j = constraint - 1; i >= 0; i--, j--)
  {
    if(s1[j] != s2[i])
    {
      return false;
    }
  }

  return true;
}

wstring
Interchunk::copycase(wstring const &source_word, wstring const &target_word)
{
  wstring result;

  bool firstupper = iswupper(source_word[0]);
  bool uppercase = firstupper && iswupper(source_word[source_word.size()-1]);
  bool sizeone = source_word.size() == 1;

  if(!uppercase || (sizeone && uppercase))
  {
    result = StringUtils::tolower(target_word);
  }
  else
  {
    result = StringUtils::toupper(target_word);
  }

  if(firstupper)
  {
    result[0] = towupper(result[0]);
  }

  return result;
}

wstring
Interchunk::caseOf(wstring const &s)
{
  if(s.size() > 1)
  {
    if(!iswupper(s[0]))
    {
      return L"aa";
    }
    else if(!iswupper(s[s.size()-1]))
    {
      return L"Aa";
    }
    else
    {
      return L"AA";
    }
  }
  else if(s.size() == 1)
  {
    if(!iswupper(s[0]))
    {
      return L"aa";
    }
    else
    {
      return L"Aa";
    }
  }
  else
  {
    return L"aa";
  }
}

StackElement
Interchunk::popStack()
{
  StackElement ret = theStack.top();
  theStack.pop();
  return ret;
}

void
Interchunk::applyRule(wstring rule)
{
  bool in_let_setup = false;
  for(int i = 0; i < rule.size(); i++)
  {
    switch(rule[i])
    {
      case L's': // string
      {
        //cout << "string" << endl;
        int ct = rule[++i];
        wstring blob;
        for(int j = 0; j < ct; j++)
        {
          blob.append(1, rule[++i]);
        }
        pushStack(blob);
      }
        break;
      case L'j': // jump
        //cout << "jump" << endl;
        i += rule[++i];
        break;
      case L'?': // test
        //cout << "test" << endl;
        if(popStack().b)
        {
          i++;
        }
        else
        {
          i += rule[++i];
        }
        break;
      case L'&': // and
        //cout << "and" << endl;
      {
        int count = rule[++i];
        bool val = true;
        for(int j = 0; j < count; j++)
        {
          val = val && popStack().b;
        }
        pushStack(val);
      }
        break;
      case L'|': // or
        //cout << "or" << endl;
      {
        int count = rule[++i];
        bool val = false;
        for(int j = 0; j < count; j++)
        {
          val = val || popStack().b;
        }
        pushStack(val);
      }
        break;
      case L'!': // not
        //cout << "not" << endl;
        theStack.top().b = !theStack.top().b;
        break;
      case L'=': // equal
        //cout << "equal" << endl;
      {
        wstring a = popStack().s;
        wstring b = popStack().s;
        if(rule[i+1] == '#')
        {
          i++;
          a = StringUtils::tolower(a);
          b = StringUtils::tolower(b);
        }
        pushStack(a == b);
      }
        break;
      case L'(': // begins-with
        //cout << "begins-with" << endl;
      {
        wstring substr = popStack().s;
        wstring str = popStack().s;
        if(rule[i+1] == '#')
        {
          i++;
          pushStack(beginsWith(StringUtils::tolower(str), StringUtils::tolower(substr)));
        }
        else
        {
          pushStack(beginsWith(str, substr));
        }
      }
        break;
      case L')': // ends-with
        //cout << "ends-with" << endl;
      {
        wstring substr = popStack().s;
        wstring str = popStack().s;
        if(rule[i+1] == '#')
        {
          i++;
          pushStack(endsWith(StringUtils::tolower(str), StringUtils::tolower(substr)));
        }
        else
        {
          pushStack(endsWith(str, substr));
        }
      }
        break;
      case L'[': // begins-with-list
        //cout << "begins-with-list" << endl;
      {
        wstring list = popStack().s;
        wstring needle = popStack().s;
        set<wstring, Ltstr>::iterator it, limit;

        if(rule[i+1] == '#')
        {
          i++;
          it = lists[list].begin();
          limit = lists[list].end();
        }
        else
        {
          needle = StringUtils::tolower(needle);
          it = listslow[list].begin();
          limit = listslow[list].end();
        }

        bool found = false;
        for(; it != limit; it++)
        {
          if(beginsWith(needle, *it))
          {
            found = true;
            break;
          }
        }
        pushStack(found);
      }
        break;
      case L']': // ends-with-list
        //cout << "ends-with-list" << endl;
      {
        wstring list = popStack().s;
        wstring needle = popStack().s;
        set<wstring, Ltstr>::iterator it, limit;

        if(rule[i+1] == '#')
        {
          i++;
          it = lists[list].begin();
          limit = lists[list].end();
        }
        else
        {
          needle = StringUtils::tolower(needle);
          it = listslow[list].begin();
          limit = listslow[list].end();
        }

        bool found = false;
        for(; it != limit; it++)
        {
          if(endsWith(needle, *it))
          {
            found = true;
            break;
          }
        }
        pushStack(found);
      }
        break;
      case L'c': // contains-substring
        //cout << "contains-substring" << endl;
      {
        wstring needle = popStack().s;
        wstring haystack = popStack().s;
        if(rule[i+1] == '#')
        {
          i++;
          needle = StringUtils::tolower(needle);
          haystack = StringUtils::tolower(haystack);
        }
        pushStack(haystack.find(needle) != wstring::npos);
      }
        break;
      case L'n': // in
        //cout << "in" << endl;
      {
        wstring list = popStack().s;
        wstring str = popStack().s;
        if(rule[i+1] == '#')
        {
          i++;
          str = StringUtils::tolower(str);
          set<wstring, Ltstr> &myset = listslow[list];
          pushStack(myset.find(str) != myset.end());
        }
        else
        {
          set<wstring, Ltstr> &myset = lists[list];
          pushStack(myset.find(str) != myset.end());
        }
      }
        break;
      case L'>': // let (begin)
        //cout << "let" << endl;
        in_let_setup = true;
        break;
      case L'*': // let (clip, end)
        //cout << "let clip" << endl;
      {
        wstring val = popStack().s;
        pair<int, wstring> clip = popStack().clip;
        if(i+1 < rule.size() && rule[i+1] == L'#')
        {
          i++;
          string temp = currentInput[2*(clip.first-1)]->chunkPart(attr_items[clip.second]);
          val = copycase(val, UtfConverter::fromUtf8(temp));
        }
        currentInput[2*(clip.first-1)]->setChunkPart(attr_items[clip.second], val);
      }
        break;
      case L'4': // let (var, end)
        //cout << "let var" << endl;
      {
        wstring val = popStack().s;
        wstring var = popStack().s;
        if(i+1 < rule.size() && rule[i+1] == L'#')
        {
          i++;
          val = copycase(val, variables[var]);
        }
        variables[var] = val;
      }
        break;
      case L'<': // out
        //cout << "out" << endl;
      {
        int count = rule[++i];
        vector<Chunk*> nodes;
        nodes.resize(count);
        for(int j = 0; j < count; j++)
        {
          nodes.push_back(popStack().c);
        }
        for(int j = 0; j < count; j++)
        {
          currentOutput.push_back(nodes.back());
          nodes.pop_back();
        }
      }
        break;
      case L'.': // clip
        //cout << "clip" << endl;
      {
        wstring part = popStack().s;
        int pos = rule[++i];
        if(in_let_setup)
        {
          pushStack(pair<int, wstring>(pos, part));
          in_let_setup = false;
        }
        else
        {
          string v = currentInput[2*(pos-1)]->chunkPart(attr_items[part]);
          pushStack(UtfConverter::fromUtf8(v));
        }
      }
        break;
      case L'$': // var
        //cout << "var" << endl;
      {
        wstring name = theStack.top().s;
        popStack();
        if(in_let_setup)
        {
          pushStack(name);
          in_let_setup = false;
        }
        else
        {
          pushStack(variables[name]);
        }
      }
        break;
      case L'G': // get-case-from, case-of
        //cout << "get-case-from or case-of" << endl;
        pushStack(caseOf(popStack().s));
        break;
      case L'+': // concat
        //cout << "concat" << endl;
      {
        int count = rule[++i];
        wstring result;
        for(int j = 0; j < count; j++)
        {
          result.append(popStack().s);
        }
        pushStack(result);
      }
        break;
      case L'{': // chunk
        //cout << "chunk" << endl;
      {
        Chunk* ch = new Chunk();
        int count = rule[++i];
        if(recursing)
        {
          vector<Chunk*> temp;
          for(int j = 0; j < count; j++)
          {
            temp.push_back(popStack().c);
          }
          for(int j = 0; j < count; j++)
          {
            ch->addPiece(temp.back());
            temp.pop_back();
          }
        }
        else
        {
          vector<wstring> temp;
          for(int j = 0; j < count; j++)
          {
            temp.push_back(popStack().s);
          }
          for(int j = 0; j < count; j++)
          {
            ch->surface += temp.back();
            temp.pop_back();
          }
        }
        pushStack(ch);
      }
        break;
      case L'p': // pseudolemma
        //cout << "pseudolemma" << endl;
        pushStack(popStack().c->surface);
        break;
      case L' ': // b
        //cout << "b" << endl;
      {
        Chunk* ch = new Chunk(L" ");
        pushStack(ch);
      }
        break;
      case L'_': // b
        //cout << "b pos" << endl;
      {
        int loc = 2*(rule[++i]-1) + 1;
        pushStack(currentInput[loc]);
      }
        break;
      default:
        cout << "unknown instruction: " << rule[i] << endl;
    }
  }
}

Chunk *
Interchunk::readToken(FILE *in)
{
  wstring content;
  wstring data;
  while(true)
  {
    int val = fgetwc_unlocked(in);
    if(feof(in) || (internal_null_flush && val == 0))
    {
      furtherInput = false;
      Chunk* ret = new Chunk(content);
      return ret;
    }
    if(val == L'\\')
    {
      content += L'\\';
      content += wchar_t(fgetwc_unlocked(in));
    }
    else if(val == L'[')
    {
      content += L'[';
      while(true)
      {
        int val2 = fgetwc_unlocked(in);
        if(val2 == L'\\')
        {
          content += L'\\';
          content += wchar_t(fgetwc_unlocked(in));
        }
        else if(val2 == L']')
        {
          content += L']';
          break;
        }
        else
        {
          content += wchar_t(val2);
        }
      }
    }
    else if(inword && val == L'{')
    {
      while(true)
      {
        int val2 = fgetwc_unlocked(in);
        if(val2 == L'\\')
        {
          data += L'\\';
          data += wchar_t(fgetwc_unlocked(in));
        }
        else if(val2 == L'}')
        {
          int val3 = char(fgetwc_unlocked(in));
          ungetwc(val3, in);

          if(val3 == L'$')
          {
            break;
          }
        }
        else
        {
          data += wchar_t(val2);
        }
      }
    }
    else if(inword && val == L'$')
    {
      inword = false;
      Chunk* ret = new Chunk(content, data);
      return ret;
    }
    else if(val == L'^')
    {
      inword = true;
      Chunk* ret = new Chunk(content);
      return ret;
    }
    else
    {
      content += wchar_t(val);
    }
  }
}

bool
Interchunk::getNullFlush(void)
{
  return null_flush;
}

void
Interchunk::setNullFlush(bool null_flush)
{
  this->null_flush = null_flush;
}

void
Interchunk::setTrace(bool trace)
{
  this->trace = trace;
}

void
Interchunk::interchunk_wrapper_null_flush(FILE *in, FILE *out)
{
  null_flush = false;
  internal_null_flush = true;

  while(!feof(in))
  {
    interchunk(in, out);
    fputwc_unlocked(L'\0', out);
    int code = fflush(out);
    if(code != 0)
    {
      wcerr << L"Could not flush output " << errno << endl;
    }
  }
  internal_null_flush = false;
  null_flush = true;
}

void
Interchunk::applyWord(Chunk& word)
{
  if(word.isBlank())
  {
    ms.step(L' ');
    return;
  }
  wstring word_str = word.surface;
  ms.step(L'^');
  for(unsigned int i = 0, limit = word_str.size(); i < limit; i++)
  {
    switch(word_str[i])
    {
      case L'\\':
        i++;
        ms.step(towlower(word_str[i]), any_char);
        break;

      case L'<':
        for(unsigned int j = i+1; j != limit; j++)
        {
          if(word_str[j] == L'>')
          {
            int symbol = alphabet(word_str.substr(i, j-i+1));
            if(symbol)
            {
              ms.step(symbol, any_tag);
            }
            else
            {
              ms.step(any_tag);
            }
            i = j;
            break;
          }
        }
        break;

      case L'{':  // ignore the unmodifiable part of the chunk
        ms.step(L'$');
        return;

      default:
        ms.step(towlower(word_str[i]), any_char);
        break;
    }
  }
  ms.step(L'$');
}

void
Interchunk::interchunk(FILE *in, FILE *out)
{
  /*if(getNullFlush())
  {
    interchunk_wrapper_null_flush(in, out);
  }*/
  if(recursing)
  {
    interchunk_recursive(in, out);
  }
  else
  {
    interchunk_linear(in, out);
  }
}

void
Interchunk::interchunk_do_pass()
{
  int layer = -1;
  bool shouldshift = false;
  for(int l = parseTower.size()-1; l >= 0; l--)
  {
    if(parseTower[l].size() >= 2*longestPattern - 1)
    {
      layer = l;
      break;
    }
  }
  if(layer == -1)
  {
    shouldshift = true;
    if(furtherInput)
    {
      return;
    }
    for(int l = 0; l < parseTower.size(); l++)
    {
      if(parseTower[l].size() > 0)
      {
        layer = l;
        break;
      }
    }
    if(layer == -1)
    {
      allDone = true;
      return;
    }
  }
  if(layer+1 == parseTower.size())
  {
    parseTower.push_back(vector<Chunk*>());
  }
  ms.init(me->getInitial());
  int rule = -1;
  for(int i = 0; i < parseTower[layer].size()+1 && i < 2*longestPattern+1; i++)
  {
    if(ms.size() == 0)
    {
      if(rule != -1)
      {
        currentInput.clear();
        currentOutput.clear();
        currentInput.assign(parseTower[layer].begin(), parseTower[layer].begin()+i-1);
        parseTower[layer].erase(parseTower[layer].begin(), parseTower[layer].begin()+i-1);
        applyRule(rule_map[rule]);
        parseTower[layer+1].insert(parseTower[layer+1].end(), currentOutput.begin(), currentOutput.end());
        currentInput.clear();
        currentOutput.clear();
      }
      else if(shouldshift || parseTower[layer].size() >= 2*(longestPattern)-1)
      {
        parseTower[layer+1].push_back(parseTower[layer][0]);
        parseTower[layer].erase(parseTower[layer].begin());
      }
      return;
    }
    int val = ms.classifyFinals(me->getFinals());
    if(val != -1)
    {
      rule = val-1;
    }
    if(i != parseTower[layer].size())
    {
      applyWord(*parseTower[layer][i]);
    }
  }
}

void
Interchunk::interchunk_linear(FILE *in, FILE *out)
{
  parseTower.push_back(vector<Chunk*>());
  parseTower.push_back(vector<Chunk*>());
  while(!allDone)
  {
    if(furtherInput)
    {
      Chunk* ch = readToken(in);
      parseTower[0].push_back(ch);
    }
    interchunk_do_pass();
    for(int i = 0; i < parseTower[1].size(); i++)
    {
      parseTower[1][i]->output(out);
      delete parseTower[1][i];
    }
    parseTower[1].clear();
  }
}

void
Interchunk::interchunk_recursive(FILE *in, FILE *out)
{
  
}