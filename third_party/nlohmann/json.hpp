#pragma once
// third_party/nlohmann/json.hpp - standards-compliant subset, C++17
// Uses named static makers to avoid constructor overload ambiguity.
// No explicit template specialisations in class scope.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace nlohmann {

namespace detail {
template<typename T> using rm = std::remove_cv_t<std::remove_reference_t<T>>;
template<typename T> struct is_bool    : std::is_same<rm<T>,bool>{};
template<typename T> struct is_sint    : std::integral_constant<bool,
    std::is_integral_v<rm<T>>&&std::is_signed_v<rm<T>>&&!is_bool<T>::value>{};
template<typename T> struct is_uint    : std::integral_constant<bool,
    std::is_integral_v<rm<T>>&&std::is_unsigned_v<rm<T>>&&!is_bool<T>::value>{};
template<typename>   struct is_vec     : std::false_type{};
template<typename U,typename A> struct is_vec<std::vector<U,A>> : std::true_type{};
template<typename T> struct vec_elem   { using type=void; };
template<typename U,typename A> struct vec_elem<std::vector<U,A>> { using type=U; };
} // namespace detail

class json {
public:
    enum class value_t : uint8_t {
        null,object,array,string,boolean,
        number_integer,number_unsigned,number_float,discarded
    };
    using object_t          = std::map<std::string,json>;
    using array_t           = std::vector<json>;
    using string_t          = std::string;
    using number_integer_t  = int64_t;
    using number_unsigned_t = uint64_t;
    using number_float_t    = double;

    struct exception : std::runtime_error {
        int id; exception(int i,const std::string& m):std::runtime_error(m),id(i){}
    };
    struct parse_error  : exception { using exception::exception; };
    struct type_error   : exception { using exception::exception; };
    struct out_of_range : exception { using exception::exception; };

private:
    value_t t_ = value_t::null;
    union V {
        object_t* obj; array_t* arr; string_t* str;
        bool boo; int64_t i; uint64_t u; double f;
        V() noexcept : i(0) {}
    } v_;

    // private tag constructor - unambiguous
    struct sint_tag{};  struct uint_tag{};
    json(int64_t  v,sint_tag) noexcept : t_(value_t::number_integer)  { v_.i=v; }
    json(uint64_t v,uint_tag) noexcept : t_(value_t::number_unsigned) { v_.u=v; }

    void destroy() noexcept {
        switch(t_){ case value_t::object:delete v_.obj;break;
                    case value_t::array: delete v_.arr;break;
                    case value_t::string:delete v_.str;break; default:break; }
        t_=value_t::null; v_.i=0;
    }
    void copy_from(const json& o) {
        t_=o.t_;
        switch(t_){
            case value_t::object:  v_.obj=new object_t(*o.v_.obj);break;
            case value_t::array:   v_.arr=new array_t(*o.v_.arr); break;
            case value_t::string:  v_.str=new string_t(*o.v_.str);break;
            case value_t::boolean: v_.boo=o.v_.boo;break;
            case value_t::number_integer:  v_.i=o.v_.i;break;
            case value_t::number_unsigned: v_.u=o.v_.u;break;
            case value_t::number_float:    v_.f=o.v_.f;break;
            default: v_.i=0;break;
        }
    }
    static const char* tname(value_t t) noexcept {
        switch(t){ case value_t::object: return "object"; case value_t::array: return "array";
                   case value_t::string: return "string"; case value_t::boolean: return "boolean";
                   case value_t::number_integer: return "integer";
                   case value_t::number_unsigned: return "unsigned";
                   case value_t::number_float: return "float"; default: return "null"; }
    }
    void chk(value_t e,const char* ctx) const {
        if(t_!=e) throw type_error(302,std::string(ctx)+": expected "+tname(e)+" got "+tname(t_));
    }

public:
    // -- named numeric makers (avoid overload ambiguity) -----------------------
    static json from_int  (int64_t  v) noexcept { return json(v, sint_tag{}); }
    static json from_uint (uint64_t v) noexcept { return json(v, uint_tag{}); }
    static json from_float(double   v) noexcept { json j; j.t_=value_t::number_float; j.v_.f=v; return j; }
    static json from_bool (bool     v) noexcept { json j; j.t_=value_t::boolean; j.v_.boo=v; return j; }
    static json from_str  (std::string v) {
        json j; j.t_=value_t::string; j.v_.str=new std::string(std::move(v)); return j;
    }
    static json array() { json j; j.t_=value_t::array;  j.v_.arr=new array_t();  return j; }
    static json object(){ json j; j.t_=value_t::object; j.v_.obj=new object_t(); return j; }

    // -- constructors (only for types that cannot be ambiguous) ----------------
    json() noexcept {}
    json(std::nullptr_t) noexcept {}
    json(const char*      s) : t_(value_t::string) { v_.str=new string_t(s); }
    json(const string_t&  s) : t_(value_t::string) { v_.str=new string_t(s); }
    json(string_t&&       s) : t_(value_t::string) { v_.str=new string_t(std::move(s)); }
    json(const object_t&  o) : t_(value_t::object) { v_.obj=new object_t(o); }
    json(object_t&&       o) : t_(value_t::object) { v_.obj=new object_t(std::move(o)); }
    json(const array_t&   a) : t_(value_t::array)  { v_.arr=new array_t(a); }
    json(array_t&&        a) : t_(value_t::array)  { v_.arr=new array_t(std::move(a)); }

    // {key,val} pairs -> object; else array
    json(std::initializer_list<json> il) {
        bool pairs=true;
        for(auto& e:il) if(!e.is_array()||e.size()!=2||!e[0].is_string()){pairs=false;break;}
        if(pairs && il.size()>0){
            t_=value_t::object; v_.obj=new object_t();
            for(auto& e:il) (*v_.obj)[*e.v_.arr->at(0).v_.str]=e.v_.arr->at(1);
        } else { t_=value_t::array; v_.arr=new array_t(il); }
    }

    json(const json& o){ copy_from(o); }
    json(json&& o) noexcept : t_(o.t_),v_(o.v_){ o.t_=value_t::null; o.v_.i=0; }
    ~json(){ destroy(); }
    json& operator=(json o) noexcept {
        destroy(); t_=o.t_; v_=o.v_; o.t_=value_t::null; o.v_.i=0; return *this;
    }

    // -- type queries ----------------------------------------------------------
    bool is_null()            const noexcept{return t_==value_t::null;}
    bool is_object()          const noexcept{return t_==value_t::object;}
    bool is_array()           const noexcept{return t_==value_t::array;}
    bool is_string()          const noexcept{return t_==value_t::string;}
    bool is_boolean()         const noexcept{return t_==value_t::boolean;}
    bool is_number_integer()  const noexcept{return t_==value_t::number_integer;}
    bool is_number_unsigned() const noexcept{return t_==value_t::number_unsigned;}
    bool is_number_float()    const noexcept{return t_==value_t::number_float;}
    bool is_number()          const noexcept{
        return t_==value_t::number_integer||t_==value_t::number_unsigned||t_==value_t::number_float;}
    bool is_discarded()       const noexcept{return t_==value_t::discarded;}
    value_t type()            const noexcept{return t_;}

    // -- access ----------------------------------------------------------------
    json& operator[](const string_t& k){
        if(t_==value_t::null){t_=value_t::object;v_.obj=new object_t();}
        chk(value_t::object,"op[]"); return (*v_.obj)[k];
    }
    const json& operator[](const string_t& k) const { chk(value_t::object,"op[] c"); return v_.obj->at(k); }
    json& operator[](size_t i){ chk(value_t::array,"op[i]"); return (*v_.arr)[i]; }
    const json& operator[](size_t i) const { chk(value_t::array,"op[i] c"); return (*v_.arr)[i]; }
    json& at(const string_t& k){ chk(value_t::object,"at"); return v_.obj->at(k); }
    const json& at(const string_t& k) const { chk(value_t::object,"at c"); return v_.obj->at(k); }
    json& at(size_t i){ chk(value_t::array,"at[i]"); return v_.arr->at(i); }
    const json& at(size_t i) const { chk(value_t::array,"at[i] c"); return v_.arr->at(i); }
    bool contains(const string_t& k) const noexcept {
        return t_==value_t::object && v_.obj->count(k)>0;
    }
    size_t size() const noexcept {
        if(t_==value_t::object) return v_.obj->size();
        if(t_==value_t::array)  return v_.arr->size();
        return 0;
    }
    bool empty() const noexcept { return t_==value_t::null||size()==0; }
    void push_back(json val){
        if(t_==value_t::null){t_=value_t::array;v_.arr=new array_t();}
        chk(value_t::array,"push_back"); v_.arr->push_back(std::move(val));
    }

    array_t::iterator       begin()      {chk(value_t::array,"begin");return v_.arr->begin();}
    array_t::iterator       end()        {chk(value_t::array,"end");  return v_.arr->end();}
    array_t::const_iterator begin() const{chk(value_t::array,"begin");return v_.arr->begin();}
    array_t::const_iterator end()   const{chk(value_t::array,"end");  return v_.arr->end();}
    object_t&       items()       {chk(value_t::object,"items");return *v_.obj;}
    const object_t& items() const {chk(value_t::object,"items");return *v_.obj;}

    // -- get<T> via if-constexpr (no in-class explicit specialisations) --------
    template<typename T>
    T get() const {
        using R = std::remove_cv_t<std::remove_reference_t<T>>;
        if constexpr (std::is_same_v<R,bool>) {
            chk(value_t::boolean,"get<bool>"); return v_.boo;
        } else if constexpr (std::is_same_v<R,std::string>) {
            chk(value_t::string,"get<string>"); return *v_.str;
        } else if constexpr (detail::is_sint<R>::value) {
            switch(t_){
                case value_t::number_integer:  return static_cast<R>(v_.i);
                case value_t::number_unsigned: return static_cast<R>(v_.u);
                case value_t::number_float:    return static_cast<R>(v_.f);
                default: throw type_error(302,"get<sint>: not a number");
            }
        } else if constexpr (detail::is_uint<R>::value) {
            switch(t_){
                case value_t::number_unsigned: return static_cast<R>(v_.u);
                case value_t::number_integer:  return static_cast<R>(v_.i);
                case value_t::number_float:    return static_cast<R>(v_.f);
                default: throw type_error(302,"get<uint>: not a number");
            }
        } else if constexpr (std::is_floating_point_v<R>) {
            switch(t_){
                case value_t::number_float:    return static_cast<R>(v_.f);
                case value_t::number_integer:  return static_cast<R>(v_.i);
                case value_t::number_unsigned: return static_cast<R>(v_.u);
                default: throw type_error(302,"get<float>: not a number");
            }
        } else if constexpr (detail::is_vec<R>::value) {
            using U = typename detail::vec_elem<R>::type;
            chk(value_t::array,"get<vector>");
            R result; result.reserve(v_.arr->size());
            for(auto& e:*v_.arr) result.push_back(e.template get<U>());
            return result;
        } else {
            R val{}; from_json(*this,val); return val;
        }
    }
    template<typename T> void get_to(T& v) const { v=get<T>(); }

    // -- dump ------------------------------------------------------------------
    std::string dump(int indent=-1,char ic=' ',bool=false,int depth=0) const {
        std::string nl=indent>=0?"\n":"",
                    sp=indent>=0?std::string(size_t((depth+1)*indent),ic):"",
                    s0=indent>=0?std::string(size_t(depth*indent),ic):"",
                    se=indent>=0?" ":"";
        switch(t_){
            case value_t::null:            return "null";
            case value_t::boolean:         return v_.boo?"true":"false";
            case value_t::number_integer:  return std::to_string(v_.i);
            case value_t::number_unsigned: return std::to_string(v_.u);
            case value_t::number_float: {
                std::ostringstream os; os.precision(15); os<<v_.f;
                std::string s=os.str();
                if(s.find('.')==std::string::npos&&s.find('e')==std::string::npos) s+=".0";
                return s;
            }
            case value_t::string: return '"'+esc(*v_.str)+'"';
            case value_t::array: {
                if(v_.arr->empty()) return "[]";
                std::string out="["+nl;
                for(size_t i=0;i<v_.arr->size();++i){
                    out+=sp+(*v_.arr)[i].dump(indent,ic,false,depth+1);
                    if(i+1<v_.arr->size()) out+=",";
                    out+=nl;
                }
                return out+s0+"]";
            }
            case value_t::object: {
                if(v_.obj->empty()) return "{}";
                std::string out="{"+nl; size_t i=0;
                for(auto&[k,val]:*v_.obj){
                    out+=sp+'"'+esc(k)+'"'+':'+se+val.dump(indent,ic,false,depth+1);
                    if(++i<v_.obj->size()) out+=",";
                    out+=nl;
                }
                return out+s0+"}";
            }
            default: return "null";
        }
    }

    // -- parse -----------------------------------------------------------------
    static json parse(const std::string& s, bool allow_exc=true){
        size_t p=0;
        try{
            json v=pval(s,p); pws(s,p);
            if(p!=s.size()) throw parse_error(101,"trailing chars at "+std::to_string(p));
            return v;
        }catch(...){ if(allow_exc) throw; json d; d.t_=value_t::discarded; return d; }
    }

    bool operator==(const json& o) const noexcept {
        if(t_!=o.t_) return false;
        switch(t_){
            case value_t::null:            return true;
            case value_t::boolean:         return v_.boo==o.v_.boo;
            case value_t::number_integer:  return v_.i==o.v_.i;
            case value_t::number_unsigned: return v_.u==o.v_.u;
            case value_t::number_float:    return v_.f==o.v_.f;
            case value_t::string:          return *v_.str==*o.v_.str;
            case value_t::array:           return *v_.arr==*o.v_.arr;
            case value_t::object:          return *v_.obj==*o.v_.obj;
            default: return false;
        }
    }
    bool operator!=(const json& o) const noexcept { return !(*this==o); }
    friend std::ostream& operator<<(std::ostream& os,const json& j){ return os<<j.dump(2); }
    friend std::istream& operator>>(std::istream& is,json& j){
        std::string s((std::istreambuf_iterator<char>(is)),std::istreambuf_iterator<char>());
        j=parse(s); return is;
    }

private:
    static std::string esc(const std::string& s){
        std::string o; o.reserve(s.size()+4);
        for(unsigned char c:s){
            switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;
                      case '\n':o+="\\n";break;case '\r':o+="\\r";break;
                      case '\t':o+="\\t";break;case '\b':o+="\\b";break;
                      case '\f':o+="\\f";break;
                      default:if(c<0x20){char b[8];std::snprintf(b,8,"\\u%04x",c);o+=b;}
                              else o+=char(c);}
        } return o;
    }
    static void pws(const std::string& s,size_t& p) noexcept {
        while(p<s.size()&&(s[p]==' '||s[p]=='\t'||s[p]=='\n'||s[p]=='\r'))++p;
    }
    static json pval(const std::string& s,size_t& p){
        pws(s,p);
        if(p>=s.size()) throw parse_error(101,"unexpected end");
        char c=s[p];
        if(c=='{') return pobj(s,p);
        if(c=='[') return parr(s,p);
        if(c=='"') return json(pstr(s,p));
        if(s.size()-p>=4&&s.substr(p,4)=="true") {p+=4;return json::from_bool(true);}
        if(s.size()-p>=5&&s.substr(p,5)=="false"){p+=5;return json::from_bool(false);}
        if(s.size()-p>=4&&s.substr(p,4)=="null") {p+=4;return json();}
        if(c=='-'||(c>='0'&&c<='9')) return pnum(s,p);
        throw parse_error(101,std::string("unexpected '")+c+"' at "+std::to_string(p));
    }
    static json pobj(const std::string& s,size_t& p){
        ++p; json obj=json::object(); pws(s,p);
        if(p<s.size()&&s[p]=='}'){++p;return obj;}
        while(p<s.size()){
            pws(s,p);
            if(p>=s.size()||s[p]!='"') throw parse_error(101,"expected key at "+std::to_string(p));
            std::string k=pstr(s,p); pws(s,p);
            if(p>=s.size()||s[p]!=':') throw parse_error(101,"expected ':' at "+std::to_string(p));
            ++p;
            json v=pval(s,p); obj[k]=std::move(v); pws(s,p);
            if(p>=s.size()) throw parse_error(101,"unterminated object");
            if(s[p]=='}'){++p;return obj;}
            if(s[p]!=',') throw parse_error(101,"expected ',' or '}'");
            ++p;
        }
        throw parse_error(101,"unterminated object");
    }
    static json parr(const std::string& s,size_t& p){
        ++p; json arr=json::array(); pws(s,p);
        if(p<s.size()&&s[p]==']'){++p;return arr;}
        while(p<s.size()){
            arr.push_back(pval(s,p)); pws(s,p);
            if(p>=s.size()) throw parse_error(101,"unterminated array");
            if(s[p]==']'){++p;return arr;}
            if(s[p]!=',') throw parse_error(101,"expected ',' or ']'");
            ++p;
        }
        throw parse_error(101,"unterminated array");
    }
    static std::string pstr(const std::string& s,size_t& p){
        if(s[p]!='"') throw parse_error(101,"expected '\"'");
        ++p; std::string o; o.reserve(32);
        while(p<s.size()&&s[p]!='"'){
            if(s[p]=='\\'){
                ++p; if(p>=s.size()) throw parse_error(101,"unterminated escape");
                switch(s[p]){
                    case '"':o+='"';break; case '\\':o+='\\';break;
                    case '/':o+='/';break; case 'n':o+='\n';break;
                    case 'r':o+='\r';break;case 't':o+='\t';break;
                    case 'b':o+='\b';break;case 'f':o+='\f';break;
                    case 'u':{
                        unsigned cp=0;
                        for(int i=0;i<4;++i){
                            ++p; if(p>=s.size()) throw parse_error(101,"short \\u");
                            char h=s[p]; cp<<=4;
                            if(h>='0'&&h<='9')cp|=unsigned(h-'0');
                            else if(h>='a'&&h<='f')cp|=unsigned(h-'a'+10);
                            else if(h>='A'&&h<='F')cp|=unsigned(h-'A'+10);
                            else throw parse_error(101,"bad hex in \\u");
                        }
                        if(cp<0x80) o+=char(cp);
                        else if(cp<0x800){o+=char(0xC0|(cp>>6));o+=char(0x80|(cp&0x3F));}
                        else{o+=char(0xE0|(cp>>12));o+=char(0x80|((cp>>6)&0x3F));o+=char(0x80|(cp&0x3F));}
                        break;
                    }
                    default:o+=s[p];
                }
            } else { o+=s[p]; }
            ++p;
        }
        if(p>=s.size()) throw parse_error(101,"unterminated string");
        ++p; return o;
    }
    static json pnum(const std::string& s,size_t& p){
        size_t st=p; bool flt=false;
        if(s[p]=='-')++p;
        while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;
        if(p<s.size()&&s[p]=='.'){flt=true;++p;while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
        if(p<s.size()&&(s[p]=='e'||s[p]=='E')){flt=true;++p;
            if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;
            while(p<s.size()&&s[p]>='0'&&s[p]<='9')++p;}
        std::string tok=s.substr(st,p-st);
        if(flt) return json::from_float(std::stod(tok));
        try{
            int64_t v=std::stoll(tok);
            if(v>=0) return json::from_uint(static_cast<uint64_t>(v));
            return json::from_int(v);
        }catch(...){ return json::from_float(std::stod(tok)); }
    }
};

// -- ADL hooks ----------------------------------------------------------------
inline void to_json(json& j,bool v)               { j=json::from_bool(v); }
inline void to_json(json& j,int v)                { j=json::from_int(static_cast<int64_t>(v)); }
inline void to_json(json& j,int64_t v)            { j=json::from_int(v); }
inline void to_json(json& j,uint64_t v)           { j=json::from_uint(v); }
inline void to_json(json& j,double v)             { j=json::from_float(v); }
inline void to_json(json& j,const std::string& v) { j=json(v); }

template<typename T>
void to_json(json& j,const std::vector<T>& vec){
    j=json::array();
    for(auto& e:vec){json ej;to_json(ej,e);j.push_back(std::move(ej));}
}

inline void from_json(const json& j,bool& v)       { v=j.get<bool>(); }
inline void from_json(const json& j,int& v)         { v=j.get<int>(); }
inline void from_json(const json& j,int64_t& v)     { v=j.get<int64_t>(); }
inline void from_json(const json& j,uint64_t& v)    { v=j.get<uint64_t>(); }
inline void from_json(const json& j,double& v)      { v=j.get<double>(); }
inline void from_json(const json& j,std::string& v) { v=j.get<std::string>(); }

template<typename T>
void from_json(const json& j,std::vector<T>& vec){
    vec.clear();
    for(size_t i=0;i<j.size();++i){T e{};from_json(j.at(i),e);vec.push_back(std::move(e));}
}

} // namespace nlohmann

using json = nlohmann::json;
