#include <errno.h>
#include <string.h>
#include "ModuleConfig.hh"
#include "str.hh"
#include "FeatureGenerator.hh"

FeatureGenerator::FeatureGenerator(void) :
  m_base_module(NULL),
  m_last_module(NULL),
  m_eof_on_last_frame(false)
{
}

FeatureGenerator::~FeatureGenerator()
{
  for (int i = 0; i < (int)m_modules.size(); i++)
    delete m_modules[i];
}

void
FeatureGenerator::open(const std::string &filename, bool raw_audio)
{
  if (m_file != NULL)
    close();

  if (raw_audio > 0)
    m_audio_format = AF_RAW;
  else
    m_audio_format = AF_AUTO;

  m_file = fopen(filename.c_str(), "rb");
  if (m_file == NULL)
    throw std::string("could not open file ") + filename + ": " +
      strerror(errno);
  
  assert( m_base_module != NULL );
  m_base_module->set_file(m_file);
}


void
FeatureGenerator::close(void)
{
  if (m_file != NULL) {
    m_base_module->discard_file();
    m_file = NULL;
  }
}


void
FeatureGenerator::load_configuration(FILE *file)
{
  assert(m_modules.empty());
  std::string line;
  std::vector<std::string> fields;
  int lineno = 0;
  while (str::read_line(&line, file, true)) {
    lineno++;
    str::clean(&line, " \t");
    if (line.empty())
      continue;
    if (line != "module")
      throw str::fmt(256, "expected keyword 'module' on line %d: ", lineno) +
	line;

    // Read module config
    //
    ModuleConfig config;
    try { 
      config.read(file);
    }
    catch (std::string &str) {
      lineno += config.num_lines_read();
      throw str::fmt(256, "failed reading feature module around line %d: ",
		     lineno) + str;
    }
    lineno += config.num_lines_read();


    // Create module
    //
    std::string type;
    std::string name;
    if (!config.get("type", type))
      throw str::fmt(256, "type not defined for module ending on line %d",
		     lineno);
    if (!config.get("name", name))
      throw str::fmt(256, "name not defined for module ending on line %d",
		     lineno);
    assert(!name.empty());
  
    FeatureModule *module = NULL;
    if (type == FFTModule::type_str()) 
      module = new FFTModule(this);
    else if (type == MelModule::type_str())
      module = new MelModule(this);
    else if (type == PowerModule::type_str())
      module = new PowerModule();
    else if (type == DCTModule::type_str())
      module = new DCTModule();
    else if (type == DeltaModule::type_str())
      module = new DeltaModule();
    else if (type == NormalizationModule::type_str())
      module = new NormalizationModule();
    else if (type == TransformationModule::type_str())
      module = new TransformationModule();
    else if (type == MergerModule::type_str())
      module = new MergerModule();
    else
      throw std::string("Unknown module type '") + type + std::string("'");
    module->set_name(name);

    // Insert module in module structures
    //
    if (m_modules.empty()) {
      m_base_module = dynamic_cast<BaseFeaModule*>(module);
      if (m_base_module == NULL)
	throw std::string("first module should be a base module");
    }
    m_last_module = module;
    m_modules.push_back(module);
    if (m_module_map.find(name) != m_module_map.end())
      throw std::string("multiple definitions of module name: ") + name;
    m_module_map[name] = module;

    // Create source links
    //
    bool has_sources = config.exists("sources");
    if (m_base_module == module && has_sources)
      throw std::string("can not define sources for the first module");
    if (m_base_module != module && !has_sources)
      throw std::string("sources not defined for module: ") + name;

    if (has_sources) {
      std::vector<std::string> sources;
      config.get("sources", sources);
      assert(!sources.empty());
      for (int i = 0; i < (int)sources.size(); i++) {
	ModuleMap::iterator it = m_module_map.find(sources[i]);
	if (it == m_module_map.end())
	  throw std::string("unknown source module: ") + sources[i];
	module->add_source(it->second);
      }
    }
    
    module->set_config(config);
  }
}


void
FeatureGenerator::write_configuration(FILE *file)
{
  assert(!m_modules.empty());
  for (int i = 0; i < (int)m_modules.size(); i++) {
    FeatureModule *module = m_modules[i];

    ModuleConfig config;
    module->get_config(config);

    if (!module->sources().empty()) {
      std::vector<std::string> sources;
      for (int i = 0; i < (int)module->sources().size(); i++)
	sources.push_back(module->sources().at(i)->name());
      config.set("sources", sources);
    }

    fputs("module\n{\n", file);
    config.write(file, 2);
    fputs("}\n\n", file);
  }
}

FeatureModule*
FeatureGenerator::module(const std::string &name)
{
  ModuleMap::iterator it = m_module_map.find(name);
  if (it == m_module_map.end())
    throw std::string("unknown module requested: ") + name;
  return it->second;
}