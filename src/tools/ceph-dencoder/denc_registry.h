// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include <iostream>
#include <string>
#include "include/buffer_fwd.h"
#include "msg/Message.h"

namespace ceph {
  class Formatter;
}

struct Dencoder {
  virtual ~Dencoder() {}
  virtual std::string decode(bufferlist bl, uint64_t seek) = 0;
  virtual void encode(bufferlist& out, uint64_t features) = 0;
  virtual void dump(ceph::Formatter *f) = 0;
  virtual void copy() {
    std::cerr << "copy operator= not supported" << std::endl;
  }
  virtual void copy_ctor() {
    std::cerr << "copy ctor not supported" << std::endl;
  }
  virtual void generate() = 0;
  virtual int num_generated() = 0;
  virtual std::string select_generated(unsigned n) = 0;
  virtual bool is_deterministic() = 0;
  //virtual void print(ostream& out) = 0;
};

template<class T>
class DencoderBase : public Dencoder {
protected:
  T* m_object;
  list<T*> m_list;
  bool stray_okay;
  bool nondeterministic;

public:
  DencoderBase(bool stray_okay, bool nondeterministic)
    : m_object(new T),
      stray_okay(stray_okay),
      nondeterministic(nondeterministic) {}
  ~DencoderBase() override {
    delete m_object;
  }

  std::string decode(bufferlist bl, uint64_t seek) override {
    auto p = bl.cbegin();
    p.seek(seek);
    try {
      using ceph::decode;
      decode(*m_object, p);
    }
    catch (buffer::error& e) {
      return e.what();
    }
    if (!stray_okay && !p.end()) {
      ostringstream ss;
      ss << "stray data at end of buffer, offset " << p.get_off();
      return ss.str();
    }
    return {};
  }

  void encode(bufferlist& out, uint64_t features) override = 0;

  void dump(ceph::Formatter *f) override {
    m_object->dump(f);
  }
  void generate() override {
    T::generate_test_instances(m_list);
  }
  int num_generated() override {
    return m_list.size();
  }
  string select_generated(unsigned i) override {
    // allow 0- or 1-based (by wrapping)
    if (i == 0)
      i = m_list.size();
    if ((i == 0) || (i > m_list.size()))
      return "invalid id for generated object";
    m_object = *(std::next(m_list.begin(), i-1));
    return string();
  }

  bool is_deterministic() override {
    return !nondeterministic;
  }
};

template<class T>
class DencoderImplNoFeatureNoCopy : public DencoderBase<T> {
public:
  DencoderImplNoFeatureNoCopy(bool stray_ok, bool nondeterministic)
    : DencoderBase<T>(stray_ok, nondeterministic) {}
  void encode(bufferlist& out, uint64_t features) override {
    out.clear();
    using ceph::encode;
    encode(*this->m_object, out);
  }
};

template<class T>
class DencoderImplNoFeature : public DencoderImplNoFeatureNoCopy<T> {
public:
  DencoderImplNoFeature(bool stray_ok, bool nondeterministic)
    : DencoderImplNoFeatureNoCopy<T>(stray_ok, nondeterministic) {}
  void copy() override {
    T *n = new T;
    *n = *this->m_object;
    delete this->m_object;
    this->m_object = n;
  }
  void copy_ctor() override {
    T *n = new T(*this->m_object);
    delete this->m_object;
    this->m_object = n;
  }
};

template<class T>
class DencoderImplFeaturefulNoCopy : public DencoderBase<T> {
public:
  DencoderImplFeaturefulNoCopy(bool stray_ok, bool nondeterministic)
    : DencoderBase<T>(stray_ok, nondeterministic) {}
  void encode(bufferlist& out, uint64_t features) override {
    out.clear();
    using ceph::encode;
    encode(*(this->m_object), out, features);
  }
};

template<class T>
class DencoderImplFeatureful : public DencoderImplFeaturefulNoCopy<T> {
public:
  DencoderImplFeatureful(bool stray_ok, bool nondeterministic)
    : DencoderImplFeaturefulNoCopy<T>(stray_ok, nondeterministic) {}
  void copy() override {
    T *n = new T;
    *n = *this->m_object;
    delete this->m_object;
    this->m_object = n;
  }
  void copy_ctor() override {
    T *n = new T(*this->m_object);
    delete this->m_object;
    this->m_object = n;
  }
};

template<class T>
class MessageDencoderImpl : public Dencoder {
  ref_t<T> m_object;
  list<ref_t<T>> m_list;

public:
  MessageDencoderImpl() : m_object{make_message<T>()} {}
  ~MessageDencoderImpl() override {}

  string decode(bufferlist bl, uint64_t seek) override {
    auto p = bl.cbegin();
    p.seek(seek);
    try {
      ref_t<Message> n(decode_message(g_ceph_context, 0, p), false);
      if (!n)
	throw std::runtime_error("failed to decode");
      if (n->get_type() != m_object->get_type()) {
	stringstream ss;
	ss << "decoded type " << n->get_type() << " instead of expected " << m_object->get_type();
	throw std::runtime_error(ss.str());
      }
      m_object = ref_cast<T>(n);
    }
    catch (buffer::error& e) {
      return e.what();
    }
    if (!p.end()) {
      ostringstream ss;
      ss << "stray data at end of buffer, offset " << p.get_off();
      return ss.str();
    }
    return string();
  }

  void encode(bufferlist& out, uint64_t features) override {
    out.clear();
    encode_message(m_object.get(), features, out);
  }

  void dump(ceph::Formatter *f) override {
    m_object->dump(f);
  }
  void generate() override {
    //T::generate_test_instances(m_list);
  }
  int num_generated() override {
    return m_list.size();
  }
  string select_generated(unsigned i) override {
    // allow 0- or 1-based (by wrapping)
    if (i == 0)
      i = m_list.size();
    if ((i == 0) || (i > m_list.size()))
      return "invalid id for generated object";
    m_object = *(std::next(m_list.begin(), i-1));
    return string();
  }
  bool is_deterministic() override {
    return true;
  }

  //void print(ostream& out) {
  //out << m_object << std::endl;
  //}
};

class DencoderRegistry
{
  using dencoders_t = std::map<std::string, std::unique_ptr<Dencoder>>;
public:
  void add(const char* name, std::unique_ptr<Dencoder>&& denc);
  dencoders_t& get() {
    return dencoders;
  }
  static DencoderRegistry& instance();
private:
  dencoders_t dencoders;
};

template<typename DencoderT, typename...Args>
void make_and_register_dencoder(const char* name, Args&&...args)
{
  auto dencoder = std::make_unique<DencoderT>(std::forward<Args>(args)...);
  DencoderRegistry::instance().add(name, std::move(dencoder));
}

#define TYPE(t) make_and_register_dencoder<DencoderImplNoFeature<t>>(#t, false, false);
#define TYPE_STRAYDATA(t) make_and_register_dencoder<DencoderImplNoFeature<t>>(#t, true, false);
#define TYPE_NONDETERMINISTIC(t) make_and_register_dencoder<DencoderImplNoFeature<t>>(#t, false, true);
#define TYPE_FEATUREFUL(t) make_and_register_dencoder<DencoderImplFeatureful<t>>(#t, false, false);
#define TYPE_FEATUREFUL_STRAYDATA(t) make_and_register_dencoder<DencoderImplFeatureful<t>>(#t, true, false);
#define TYPE_FEATUREFUL_NONDETERMINISTIC(t) make_and_register_dencoder<DencoderImplFeatureful<t>>(#t, false, true);
#define TYPE_FEATUREFUL_NOCOPY(t) make_and_register_dencoder<DencoderImplFeaturefulNoCopy<t>>(#t, false, false);
#define TYPE_NOCOPY(t) make_and_register_dencoder<DencoderImplNoFeatureNoCopy<t>>(#t, false, false);
#define MESSAGE(t) make_and_register_dencoder<MessageDencoderImpl<t>>(#t);

void register_common_dencoders();
void register_mds_dencoders();
void register_osd_dencoders();
void register_rbd_dencoders();
void register_rgw_dencoders();
