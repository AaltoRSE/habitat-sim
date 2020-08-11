// Copyright (c) Facebook, Inc. and its affiliates.
// This source code is licensed under the MIT license found in the
// LICENSE file in the root directory of this source tree.

// Code adapted from Bullet3/examples/Importers/ImportURDFDemo ...

#include <Corrade/Utility/DebugStl.h>
#include <Corrade/Utility/Directory.h>
#include <Corrade/Utility/String.h>
#include <Magnum/Math/Quaternion.h>

#include "URDFParser.h"

#include "tinyxml2/tinyxml2.h"

using namespace tinyxml2;

namespace Cr = Corrade;
namespace Mn = Magnum;

namespace esp {
namespace io {

bool URDFParser::parseURDF(const std::string& filename) {
  UrdfModel newURDFModel = UrdfModel();
  sourceFilePath_ = filename;
  newURDFModel.m_sourceFile = filename;

  std::string xmlString = Corrade::Utility::Directory::readString(filename);

  XMLDocument xml_doc;
  xml_doc.Parse(xmlString.c_str());
  if (xml_doc.Error()) {
    Corrade::Utility::Debug()
        << "URDFParser::parseURDF - XML parse error, aborting URDF parse/load.";
    return false;
  }
  Corrade::Utility::Debug()
      << "URDFParser::parseURDF - XML parsed starting URDF parse/load.";

  XMLElement* robot_xml = xml_doc.FirstChildElement("robot");
  if (!robot_xml) {
    Corrade::Utility::Debug() << "E - expected a robot element";
    return false;
  }

  // Get robot name
  const char* name = robot_xml->Attribute("name");
  if (!name) {
    Corrade::Utility::Debug() << "E - expected a name for robot";
    return false;
  }
  newURDFModel.m_name = name;

  // Get all Material elements
  for (XMLElement* material_xml = robot_xml->FirstChildElement("material");
       material_xml;
       material_xml = material_xml->NextSiblingElement("material")) {
    std::shared_ptr<UrdfMaterial> material = std::make_shared<UrdfMaterial>();

    parseMaterial(*material.get(), material_xml);

    if (newURDFModel.m_materials.count(material->m_name) == 0) {
      newURDFModel.m_materials[material->m_name] = material;
    } else {
      Corrade::Utility::Debug() << "W - Duplicate material";
    }
  }

  // Get all link elements including shapes
  for (XMLElement* link_xml = robot_xml->FirstChildElement("link"); link_xml;
       link_xml = link_xml->NextSiblingElement("link")) {
    std::shared_ptr<UrdfLink> link = std::make_shared<UrdfLink>();

    if (parseLink(newURDFModel, *link.get(), link_xml)) {
      if (newURDFModel.m_links.count(link->m_name)) {
        Corrade::Utility::Debug() << "E - Link name is not unique, link names "
                                     "in the same model have to be unique";
        Corrade::Utility::Debug() << "E - " << link->m_name;
        return false;
      } else {
        // copy model material into link material, if link has no local material
        for (size_t i = 0; i < link->m_visualArray.size(); i++) {
          UrdfVisual& vis = link->m_visualArray.at(i);
          if (!vis.m_geometry.m_hasLocalMaterial &&
              !vis.m_materialName.empty()) {
            auto mat_itr = newURDFModel.m_materials.find(vis.m_materialName);
            if (mat_itr != newURDFModel.m_materials.end()) {
              vis.m_geometry.m_localMaterial = mat_itr->second;
            } else {
              Corrade::Utility::Debug()
                  << "E - Cannot find material with name: "
                  << vis.m_materialName;
            }
          }
        }

        // register the new link
        newURDFModel.m_links[link->m_name] = link;
      }
    } else {
      Corrade::Utility::Debug() << "E - failed to parse link";
      return false;
    }
  }
  if (newURDFModel.m_links.size() == 0) {
    Corrade::Utility::Debug() << "W - No links found in URDF file.";
    return false;
  }

  // Get all Joint elements
  for (XMLElement* joint_xml = robot_xml->FirstChildElement("joint"); joint_xml;
       joint_xml = joint_xml->NextSiblingElement("joint")) {
    std::shared_ptr<UrdfJoint> joint = std::make_shared<UrdfJoint>();

    if (parseJoint(*joint.get(), joint_xml)) {
      if (newURDFModel.m_joints.count(joint->m_name)) {
        Corrade::Utility::Debug()
            << "E - joint " << joint->m_name << " is not unique";
        return false;
      } else {
        newURDFModel.m_joints[joint->m_name] = joint;
      }
    } else {
      Corrade::Utility::Debug() << "E - joint xml is not initialized correctly";
      return false;
    }
  }

  // TODO: parse sensors here

  if (!initTreeAndRoot(newURDFModel)) {
    return false;
  }
  m_urdfModel = newURDFModel;

  Corrade::Utility::Debug() << "Done parsing URDF";

  return true;
}

static bool parseColor4(Mn::Color4& color4, const std::string& vector_str) {
  color4 = Mn::Color4();  // 0 init
  std::vector<std::string> pieces =
      Corrade::Utility::String::splitWithoutEmptyParts(vector_str);
  std::vector<double> vec_parts;
  for (size_t i = 0; i < pieces.size(); ++i) {
    if (!pieces[i].empty()) {
      vec_parts.push_back(std::stod(pieces[i]));
    }
  }
  if (vec_parts.size() != 4) {
    return false;
  }
  color4 = Mn::Color4(vec_parts[0], vec_parts[1], vec_parts[2], vec_parts[3]);
  return true;
}

static bool parseVector3(Mn::Vector3& vec3,
                         const std::string& vector_str,
                         bool lastThree = false) {
  vec3 = Mn::Vector3();  // 0 init
  std::vector<std::string> pieces =
      Corrade::Utility::String::splitWithoutEmptyParts(vector_str);
  std::vector<double> vec_parts;
  for (size_t i = 0; i < pieces.size(); ++i) {
    if (!pieces[i].empty()) {
      vec_parts.push_back(std::stod(pieces[i]));
    }
  }
  if (vec_parts.size() < 3) {
    return false;
  }
  if (lastThree) {
    vec3 = Mn::Vector3(vec_parts[vec_parts.size() - 3],
                       vec_parts[vec_parts.size() - 2],
                       vec_parts[vec_parts.size() - 1]);
  } else {
    vec3 = Mn::Vector3(vec_parts[0], vec_parts[1], vec_parts[2]);
  }
  return true;
}

bool URDFParser::parseMaterial(UrdfMaterial& material, XMLElement* config) {
  if (!config->Attribute("name")) {
    Corrade::Utility::Debug() << "E - Material must contain a name attribute";
    return false;
  }
  material.m_name = config->Attribute("name");

  // texture
  XMLElement* t = config->FirstChildElement("texture");
  if (t) {
    if (t->Attribute("filename")) {
      material.m_textureFilename = t->Attribute("filename");
    }
  }

  // color
  {
    XMLElement* c = config->FirstChildElement("color");
    if (c) {
      if (c->Attribute("rgba")) {
        if (!parseColor4(material.m_matColor.m_rgbaColor,
                         c->Attribute("rgba"))) {
          Corrade::Utility::Debug()
              << "W - " << material.m_name << " has no rgba";
        }
      }
    }
  }

  {
    // specular (non-standard)
    XMLElement* s = config->FirstChildElement("specular");
    if (s) {
      if (s->Attribute("rgb")) {
        if (!parseVector3(material.m_matColor.m_specularColor,
                          s->Attribute("rgb"))) {
        }
      }
    }
  }
  return true;
}

bool URDFParser::parseLink(UrdfModel& model,
                           UrdfLink& link,
                           XMLElement* config) {
  const char* linkName = config->Attribute("name");
  if (!linkName) {
    Corrade::Utility::Debug() << "E - Link with no name";
    return false;
  }
  Corrade::Utility::Debug() << "------------------------------------";
  Corrade::Utility::Debug() << "URDFParser::parseLink: " << linkName;
  link.m_name = linkName;

  {
    // optional 'contact' parameters
    XMLElement* ci = config->FirstChildElement("contact");
    if (ci) {
      XMLElement* damping_xml = ci->FirstChildElement("inertia_scaling");
      if (damping_xml) {
        if (!damping_xml->Attribute("value")) {
          Corrade::Utility::Debug()
              << "E - Link/contact: damping element must have value attribute";
          return false;
        }

        link.m_contactInfo.m_inertiaScaling =
            std::stod(damping_xml->Attribute("value"));
        link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_INERTIA_SCALING;
      }
      {
        XMLElement* friction_xml = ci->FirstChildElement("lateral_friction");
        if (friction_xml) {
          if (!friction_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: lateral_friction "
                                         "element must have value attribute";
            return false;
          }

          link.m_contactInfo.m_lateralFriction =
              std::stod(friction_xml->Attribute("value"));
        }
      }

      {
        XMLElement* rolling_xml = ci->FirstChildElement("rolling_friction");
        if (rolling_xml) {
          if (!rolling_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: rolling friction "
                                         "element must have value attribute";
            return false;
          }

          link.m_contactInfo.m_rollingFriction =
              std::stod(rolling_xml->Attribute("value"));
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_ROLLING_FRICTION;
        }
      }

      {
        XMLElement* restitution_xml = ci->FirstChildElement("restitution");
        if (restitution_xml) {
          if (!restitution_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: restitution "
                                         "element must have value attribute";
            return false;
          }

          link.m_contactInfo.m_restitution =
              std::stod(restitution_xml->Attribute("value"));
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_RESTITUTION;
        }
      }

      {
        XMLElement* spinning_xml = ci->FirstChildElement("spinning_friction");
        if (spinning_xml) {
          if (!spinning_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: spinning friction "
                                         "element must have value attribute";
            return false;
          }

          link.m_contactInfo.m_spinningFriction =
              std::stod(spinning_xml->Attribute("value"));
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_SPINNING_FRICTION;
        }
      }
      {
        XMLElement* friction_anchor = ci->FirstChildElement("friction_anchor");
        if (friction_anchor) {
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_FRICTION_ANCHOR;
        }
      }
      {
        XMLElement* stiffness_xml = ci->FirstChildElement("stiffness");
        if (stiffness_xml) {
          if (!stiffness_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: stiffness element "
                                         "must have value attribute";
            return false;
          }

          link.m_contactInfo.m_contactStiffness =
              std::stod(stiffness_xml->Attribute("value"));
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_STIFFNESS_DAMPING;
        }
      }
      {
        XMLElement* damping_xml = ci->FirstChildElement("damping");
        if (damping_xml) {
          if (!damping_xml->Attribute("value")) {
            Corrade::Utility::Debug() << "E - Link/contact: damping element "
                                         "must have value attribute";
            return false;
          }

          link.m_contactInfo.m_contactDamping =
              std::stod(damping_xml->Attribute("value"));
          link.m_contactInfo.m_flags |= URDF_CONTACT_HAS_STIFFNESS_DAMPING;
        }
      }
    }
  }

  // Inertial (optional)
  XMLElement* i = config->FirstChildElement("inertial");
  if (i) {
    if (!parseInertia(link.m_inertia, i)) {
      Corrade::Utility::Debug()
          << "E - Could not parse inertial element for Link:";
      Corrade::Utility::Debug() << link.m_name;
      return false;
    }
  } else {
    if ((strlen(linkName) == 5) && (strncmp(linkName, "world", 5)) == 0) {
      link.m_inertia.m_mass = 0.f;
      link.m_inertia.m_linkLocalFrame = Mn::Matrix4();  // Identity
      link.m_inertia.m_ixx = 0.f;
      link.m_inertia.m_iyy = 0.f;
      link.m_inertia.m_izz = 0.f;
    } else {
      Corrade::Utility::Debug()
          << "W - No inertial data for link: " << link.m_name
          << ", using mass=1, localinertiadiagonal = 1,1,1, identity local "
             "inertial frame";
      link.m_inertia.m_mass = 1.f;
      link.m_inertia.m_linkLocalFrame = Mn::Matrix4();  // Identity
      link.m_inertia.m_ixx = 1.f;
      link.m_inertia.m_iyy = 1.f;
      link.m_inertia.m_izz = 1.f;
    }
  }

  // Multiple Visuals (optional)
  for (XMLElement* vis_xml = config->FirstChildElement("visual"); vis_xml;
       vis_xml = vis_xml->NextSiblingElement("visual")) {
    UrdfVisual visual;

    if (parseVisual(model, visual, vis_xml)) {
      link.m_visualArray.push_back(visual);
    } else {
      Corrade::Utility::Debug()
          << "E - Could not parse visual element for Link:" << link.m_name;
      return false;
    }
  }

  // Multiple Collisions (optional)
  for (XMLElement* col_xml = config->FirstChildElement("collision"); col_xml;
       col_xml = col_xml->NextSiblingElement("collision")) {
    UrdfCollision col;

    if (parseCollision(col, col_xml)) {
      link.m_collisionArray.push_back(col);
    } else {
      Corrade::Utility::Debug()
          << "E - Could not parse collision element for Link:"
          << link.m_name.c_str();
      return false;
    }
  }
  return true;
}

bool URDFParser::parseCollision(UrdfCollision& collision, XMLElement* config) {
  collision.m_linkLocalFrame = Mn::Matrix4();  // Identity

  // Origin
  XMLElement* o = config->FirstChildElement("origin");
  if (o) {
    if (!parseTransform(collision.m_linkLocalFrame, o))
      return false;
  }
  // Geometry
  XMLElement* geom = config->FirstChildElement("geometry");
  if (!parseGeometry(collision.m_geometry, geom)) {
    return false;
  }

  {
    const char* group_char = config->Attribute("group");
    if (group_char) {
      collision.m_flags |= URDF_HAS_COLLISION_GROUP;
      collision.m_collisionGroup = std::stoi(group_char);
    }
  }

  {
    const char* mask_char = config->Attribute("mask");
    if (mask_char) {
      collision.m_flags |= URDF_HAS_COLLISION_MASK;
      collision.m_collisionMask = std::stoi(mask_char);
    }
  }

  const char* name_char = config->Attribute("name");
  if (name_char)
    collision.m_name = name_char;

  const char* concave_char = config->Attribute("concave");
  if (concave_char)
    collision.m_flags |= URDF_FORCE_CONCAVE_TRIMESH;

  return true;
}

bool URDFParser::parseVisual(UrdfModel& model,
                             UrdfVisual& visual,
                             XMLElement* config) {
  visual.m_linkLocalFrame = Mn::Matrix4();  // Identity

  // Origin
  XMLElement* o = config->FirstChildElement("origin");
  if (o) {
    if (!parseTransform(visual.m_linkLocalFrame, o))
      return false;
  }
  // Geometry
  XMLElement* geom = config->FirstChildElement("geometry");
  if (!parseGeometry(visual.m_geometry, geom)) {
    return false;
  }

  const char* name_char = config->Attribute("name");
  if (name_char)
    visual.m_name = name_char;

  visual.m_geometry.m_hasLocalMaterial = false;

  // Material
  XMLElement* mat = config->FirstChildElement("material");
  // todo(erwincoumans) skip materials in SDF for now (due to complexity)
  if (mat) {
    // get material name
    if (!mat->Attribute("name")) {
      Corrade::Utility::Debug()
          << "E - Visual material must contain a name attribute";
      return false;
    }
    visual.m_materialName = mat->Attribute("name");

    // try to parse material element in place
    XMLElement* t = mat->FirstChildElement("texture");
    XMLElement* c = mat->FirstChildElement("color");
    XMLElement* s = mat->FirstChildElement("specular");
    if (t || c || s) {
      if (!visual.m_geometry.m_localMaterial.get()) {
        // create a new material
        visual.m_geometry.m_localMaterial = std::make_shared<UrdfMaterial>();
      }
      if (parseMaterial(*visual.m_geometry.m_localMaterial.get(), mat)) {
        // override if not new
        model.m_materials[visual.m_materialName] =
            visual.m_geometry.m_localMaterial;
        visual.m_geometry.m_hasLocalMaterial = true;
      }
    }
  }

  return true;
}

bool URDFParser::parseTransform(Mn::Matrix4& tr, XMLElement* xml) {
  tr = Mn::Matrix4();  // Identity

  Mn::Vector3 vec(0, 0, 0);

  const char* xyz_str = xml->Attribute("xyz");
  if (xyz_str) {
    parseVector3(vec, std::string(xyz_str));
  }

  tr.translation() = (vec * m_urdfScaling);

  const char* rpy_str = xml->Attribute("rpy");
  if (rpy_str != NULL) {
    Mn::Vector3 rpy;
    if (parseVector3(rpy, std::string(rpy_str))) {
      double phi, the, psi;
      double roll = rpy[0];
      double pitch = rpy[1];
      double yaw = rpy[2];

      phi = roll / 2.0;
      the = pitch / 2.0;
      psi = yaw / 2.0;

      Mn::Quaternion orn(
          Mn::Vector3(
              sin(phi) * cos(the) * cos(psi) - cos(phi) * sin(the) * sin(psi),
              cos(phi) * sin(the) * cos(psi) + sin(phi) * cos(the) * sin(psi),
              cos(phi) * cos(the) * sin(psi) - sin(phi) * sin(the) * cos(psi)),
          cos(phi) * cos(the) * cos(psi) + sin(phi) * sin(the) * sin(psi));

      tr = Magnum::Matrix4::from(orn.normalized().toMatrix(), tr.translation());
    }
  }

  return true;
}

bool URDFParser::parseGeometry(UrdfGeometry& geom, XMLElement* g) {
  if (g == 0)
    return false;

  XMLElement* shape = g->FirstChildElement();
  if (!shape) {
    Corrade::Utility::Debug() << "E - Geometry tag contains no child element.";
    return false;
  }

  // const std::string type_name = shape->ValueTStr().c_str();
  const std::string type_name = shape->Value();
  if (type_name == "sphere") {
    geom.m_type = URDF_GEOM_SPHERE;

    if (!shape->Attribute("radius")) {
      Corrade::Utility::Debug()
          << "E - Sphere shape must have a radius attribute";
      return false;
    } else {
      geom.m_sphereRadius =
          m_urdfScaling * std::stod(shape->Attribute("radius"));
    }
  } else if (type_name == "box") {
    geom.m_type = URDF_GEOM_BOX;
    if (!shape->Attribute("size")) {
      Corrade::Utility::Debug() << "E - box requires a size attribute";
      return false;
    } else {
      parseVector3(geom.m_boxSize, shape->Attribute("size"));
      geom.m_boxSize *= m_urdfScaling;
    }
  } else if (type_name == "cylinder") {
    geom.m_type = URDF_GEOM_CYLINDER;
    geom.m_hasFromTo = false;
    geom.m_capsuleRadius = 0.1;
    geom.m_capsuleHeight = 0.1;

    if (!shape->Attribute("length") || !shape->Attribute("radius")) {
      Corrade::Utility::Debug()
          << "E - Cylinder shape must have both length and radius attributes";
      return false;
    }
    geom.m_capsuleRadius =
        m_urdfScaling * std::stod(shape->Attribute("radius"));
    geom.m_capsuleHeight =
        m_urdfScaling * std::stod(shape->Attribute("length"));

  } else if (type_name == "capsule") {
    geom.m_type = URDF_GEOM_CAPSULE;
    geom.m_hasFromTo = false;

    if (!shape->Attribute("length") || !shape->Attribute("radius")) {
      Corrade::Utility::Debug()
          << "E - Capsule shape must have both length and radius attributes";
      return false;
    }
    geom.m_capsuleRadius =
        m_urdfScaling * std::stod(shape->Attribute("radius"));
    geom.m_capsuleHeight =
        m_urdfScaling * std::stod(shape->Attribute("length"));

  } else if (type_name == "mesh") {
    geom.m_type = URDF_GEOM_MESH;
    geom.m_meshScale = Mn::Vector3(1.0, 1.0, 1.0);
    std::string fn;

    if (shape->Attribute("filename")) {
      fn = shape->Attribute("filename");
    }
    if (shape->Attribute("scale")) {
      if (!parseVector3(geom.m_meshScale, shape->Attribute("scale"))) {
        Corrade::Utility::Debug() << "W - Scale should be a vector3, not "
                                     "single scalar. Workaround activated.";
        std::string scalar_str = shape->Attribute("scale");
        double scaleFactor = std::stod(scalar_str.c_str());
        if (scaleFactor) {
          geom.m_meshScale = Mn::Vector3(scaleFactor);
        }
      }
    }

    geom.m_meshScale *= m_urdfScaling;

    if (fn.empty()) {
      Corrade::Utility::Debug() << "E - Mesh filename is empty";
      return false;
    }

    geom.m_meshFileName = fn;

    // load the mesh and modify the filename to full path to reference loaded
    // asset
    bool success = validateMeshFile(geom.m_meshFileName);

    if (!success) {
      // warning already printed
      return false;
    }
  } else {
    if (type_name == "plane") {
      geom.m_type = URDF_GEOM_PLANE;

      if (!shape->Attribute("normal")) {
        Corrade::Utility::Debug() << "E - Plane requires a normal attribute";
        return false;
      } else {
        parseVector3(geom.m_planeNormal, shape->Attribute("normal"));
      }
    } else {
      Corrade::Utility::Debug() << "E - Unknown geometry type: " << type_name;
      return false;
    }
  }

  return true;
}

bool URDFParser::parseInertia(UrdfInertia& inertia, XMLElement* config) {
  inertia.m_linkLocalFrame = Mn::Matrix4();  // Identity
  inertia.m_mass = 0.f;

  // Origin
  XMLElement* o = config->FirstChildElement("origin");
  if (o) {
    if (!parseTransform(inertia.m_linkLocalFrame, o)) {
      return false;
    }
  }

  XMLElement* mass_xml = config->FirstChildElement("mass");
  if (!mass_xml) {
    Corrade::Utility::Debug()
        << "E - Inertial element must have a mass element";
    return false;
  }

  if (!mass_xml->Attribute("value")) {
    Corrade::Utility::Debug()
        << "E - Inertial: mass element must have value attribute";
    return false;
  }

  inertia.m_mass = std::stod(mass_xml->Attribute("value"));

  XMLElement* inertia_xml = config->FirstChildElement("inertia");
  if (!inertia_xml) {
    Corrade::Utility::Debug()
        << "E - Inertial element must have inertia element";
    return false;
  }

  if (!(inertia_xml->Attribute("ixx") && inertia_xml->Attribute("ixy") &&
        inertia_xml->Attribute("ixz") && inertia_xml->Attribute("iyy") &&
        inertia_xml->Attribute("iyz") && inertia_xml->Attribute("izz"))) {
    if ((inertia_xml->Attribute("ixx") && inertia_xml->Attribute("iyy") &&
         inertia_xml->Attribute("izz"))) {
      inertia.m_ixx = std::stod(inertia_xml->Attribute("ixx"));
      inertia.m_ixy = 0;
      inertia.m_ixz = 0;
      inertia.m_iyy = std::stod(inertia_xml->Attribute("iyy"));
      inertia.m_iyz = 0;
      inertia.m_izz = std::stod(inertia_xml->Attribute("izz"));
    } else {
      Corrade::Utility::Debug() << "E - Inertial: inertia element must have "
                                   "ixx,ixy,ixz,iyy,iyz,izz attributes";
      return false;
    }
  } else {
    inertia.m_ixx = std::stod(inertia_xml->Attribute("ixx"));
    inertia.m_ixy = std::stod(inertia_xml->Attribute("ixy"));
    inertia.m_ixz = std::stod(inertia_xml->Attribute("ixz"));
    inertia.m_iyy = std::stod(inertia_xml->Attribute("iyy"));
    inertia.m_iyz = std::stod(inertia_xml->Attribute("iyz"));
    inertia.m_izz = std::stod(inertia_xml->Attribute("izz"));
  }

  return true;
}

bool URDFParser::validateMeshFile(std::string& meshFilename) {
  std::string urdfDirectory =
      sourceFilePath_.substr(0, sourceFilePath_.find_last_of("/"));

  std::string meshFilePath =
      Corrade::Utility::Directory::join(urdfDirectory, meshFilename);

  bool meshSuccess = false;
  // defer asset loading to instancing time. Check asset file existance here.
  meshSuccess = Corrade::Utility::Directory::exists(meshFilePath);

  if (meshSuccess) {
    // modify the meshFilename to full filepath to enable access into
    // ResourceManager assets later.
    meshFilename = meshFilePath;
  } else {
    Corrade::Utility::Debug()
        << "Mesh file \"" << meshFilePath << "\"does not exist.";
  }

  return meshSuccess;
}

bool URDFParser::initTreeAndRoot(UrdfModel& model) {
  // every link has children links and joints, but no parents, so we create a
  // local convenience data structure for keeping child->parent relations
  std::map<std::string, std::string> parentLinkTree;

  // loop through all joints, for every link, assign children links and children
  // joints
  for (auto itr = model.m_joints.begin(); itr != model.m_joints.end(); itr++) {
    auto joint = itr->second;

    std::string parent_link_name = joint->m_parentLinkName;
    std::string child_link_name = joint->m_childLinkName;
    if (parent_link_name.empty() || child_link_name.empty()) {
      Corrade::Utility::Debug()
          << "E - parent link or child link is empty for joint: "
          << joint->m_name;
      return false;
    }

    if (!model.m_links.count(joint->m_childLinkName)) {
      Corrade::Utility::Debug()
          << "E - Cannot find child link for joint: " << joint->m_name
          << ", child: " << joint->m_childLinkName;
      return false;
    }
    auto childLink = model.m_links.at(joint->m_childLinkName);

    if (!model.m_links.count(joint->m_parentLinkName)) {
      Corrade::Utility::Debug()
          << "E - Cannot find parent link for joint: " << joint->m_name
          << ", parent: " << joint->m_parentLinkName;
      return false;
    }
    auto parentLink = model.m_links.at(joint->m_parentLinkName);

    childLink->m_parentLink = parentLink;

    childLink->m_parentJoint = joint;
    parentLink->m_childJoints.push_back(joint);
    parentLink->m_childLinks.push_back(childLink);
    parentLinkTree[childLink->m_name] = parentLink->m_name;
  }

  // search for children that have no parent, those are 'root'
  int index = 0;
  for (auto itr = model.m_links.begin(); itr != model.m_links.end(); itr++) {
    auto link = itr->second;

    link->m_linkIndex = index;
    model.m_linkIndicesToNames[link->m_linkIndex] = link->m_name;

    if (!link->m_parentLink) {
      model.m_rootLinks.push_back(link);
    }
    index++;
  }

  if (model.m_rootLinks.size() > 1) {
    Corrade::Utility::Debug()
        << "W - URDF file with multiple root links found:";

    for (size_t i = 0; i < model.m_rootLinks.size(); i++) {
      Corrade::Utility::Debug() << model.m_rootLinks[i]->m_name;
    }
  }

  if (model.m_rootLinks.size() == 0) {
    Corrade::Utility::Debug() << "E - URDF without root link found.";
    return false;
  }
  return true;
}

bool URDFParser::parseJointLimits(UrdfJoint& joint,
                                  tinyxml2::XMLElement* config) {
  joint.m_lowerLimit = 0.f;
  joint.m_upperLimit = -1.f;
  joint.m_effortLimit = 0.f;
  joint.m_velocityLimit = 0.f;
  joint.m_jointDamping = 0.f;
  joint.m_jointFriction = 0.f;

  const char* lower_str = config->Attribute("lower");
  if (lower_str) {
    joint.m_lowerLimit = std::stod(lower_str);
  }

  const char* upper_str = config->Attribute("upper");
  if (upper_str) {
    joint.m_upperLimit = std::stod(upper_str);
  }

  if (joint.m_type == URDFPrismaticJoint) {
    joint.m_lowerLimit *= m_urdfScaling;
    joint.m_upperLimit *= m_urdfScaling;
  }

  // Get joint effort limit
  const char* effort_str = config->Attribute("effort");
  if (effort_str) {
    joint.m_effortLimit = std::stod(effort_str);
  }

  // Get joint velocity limit
  const char* velocity_str = config->Attribute("velocity");
  if (velocity_str) {
    joint.m_velocityLimit = std::stod(velocity_str);
  }

  return true;
}

bool URDFParser::parseJointDynamics(UrdfJoint& joint,
                                    tinyxml2::XMLElement* config) {
  joint.m_jointDamping = 0;
  joint.m_jointFriction = 0;

  // Get joint damping
  const char* damping_str = config->Attribute("damping");
  if (damping_str) {
    joint.m_jointDamping = std::stod(damping_str);
  }

  // Get joint friction
  const char* friction_str = config->Attribute("friction");
  if (friction_str) {
    joint.m_jointFriction = std::stod(friction_str);
  }

  if (damping_str == NULL && friction_str == NULL) {
    Corrade::Utility::Debug() << "E - joint dynamics element specified with no "
                                 "damping and no friction";
    return false;
  }

  return true;
}

bool URDFParser::parseJoint(UrdfJoint& joint, tinyxml2::XMLElement* config) {
  // Get Joint Name
  const char* name = config->Attribute("name");
  if (!name) {
    Corrade::Utility::Debug() << "E - unnamed joint found";
    return false;
  }
  joint.m_name = name;
  joint.m_parentLinkToJointTransform = Mn::Matrix4();  // Identity

  // Get transform from Parent Link to Joint Frame
  XMLElement* origin_xml = config->FirstChildElement("origin");
  if (origin_xml) {
    if (!parseTransform(joint.m_parentLinkToJointTransform, origin_xml)) {
      Corrade::Utility::Debug()
          << "E - Malformed parent origin element for joint: " << joint.m_name;
      return false;
    }
  }

  // Get Parent Link
  XMLElement* parent_xml = config->FirstChildElement("parent");
  if (parent_xml) {
    const char* pname = parent_xml->Attribute("link");
    if (!pname) {
      Corrade::Utility::Debug() << "E - no parent link name specified for "
                                   "Joint link. this might be the root? "
                                << joint.m_name;
      return false;
    } else {
      joint.m_parentLinkName = std::string(pname);
    }
  }

  // Get Child Link
  XMLElement* child_xml = config->FirstChildElement("child");
  if (child_xml) {
    const char* pname = child_xml->Attribute("link");
    if (!pname) {
      Corrade::Utility::Debug()
          << "E - no child link name specified for Joint link " << joint.m_name;
      return false;
    } else {
      joint.m_childLinkName = std::string(pname);
    }
  }

  // Get Joint type
  const char* type_char = config->Attribute("type");
  if (!type_char) {
    Corrade::Utility::Debug()
        << "E - joint " << joint.m_name
        << " has no type, check to see if it's a reference.";
    return false;
  }

  std::string type_str = type_char;
  if (type_str == "spherical")
    joint.m_type = URDFSphericalJoint;
  else if (type_str == "planar")
    joint.m_type = URDFPlanarJoint;
  else if (type_str == "floating")
    joint.m_type = URDFFloatingJoint;
  else if (type_str == "revolute")
    joint.m_type = URDFRevoluteJoint;
  else if (type_str == "continuous")
    joint.m_type = URDFContinuousJoint;
  else if (type_str == "prismatic")
    joint.m_type = URDFPrismaticJoint;
  else if (type_str == "fixed")
    joint.m_type = URDFFixedJoint;
  else {
    Corrade::Utility::Debug()
        << "E - Joint " << joint.m_name << " has unkown type: " << type_str;
    return false;
  }

  // Get Joint Axis
  if (joint.m_type != URDFFloatingJoint && joint.m_type != URDFFixedJoint) {
    // axis
    XMLElement* axis_xml = config->FirstChildElement("axis");
    if (!axis_xml) {
      Corrade::Utility::Debug() << "W - urdfdom: no axis elemement for Joint, "
                                   "defaulting to (1,0,0) axis "
                                << joint.m_name;
      joint.m_localJointAxis = Mn::Vector3(1, 0, 0);
    } else {
      if (axis_xml->Attribute("xyz")) {
        if (!parseVector3(joint.m_localJointAxis, axis_xml->Attribute("xyz"))) {
          Corrade::Utility::Debug()
              << "E - Malformed axis element: " << axis_xml->Attribute("xyz")
              << " for joint: " << joint.m_name;
          return false;
        }
      }
    }
  }

  // Get limit
  XMLElement* limit_xml = config->FirstChildElement("limit");
  if (limit_xml) {
    if (!parseJointLimits(joint, limit_xml)) {
      Corrade::Utility::Debug()
          << "E - Could not parse limit element for joint:" << joint.m_name;
      return false;
    }
  } else if (joint.m_type == URDFRevoluteJoint) {
    Corrade::Utility::Debug()
        << "E - Joint is of type REVOLUTE but it does not specify limits: "
        << joint.m_name;
    return false;
  } else if (joint.m_type == URDFPrismaticJoint) {
    Corrade::Utility::Debug()
        << "E - Joint is of type PRISMATIC without limits: " << joint.m_name;
    return false;
  }

  joint.m_jointDamping = 0;
  joint.m_jointFriction = 0;

  // Get Dynamics
  XMLElement* prop_xml = config->FirstChildElement("dynamics");
  if (prop_xml) {
    // Get joint damping
    const char* damping_str = prop_xml->Attribute("damping");
    if (damping_str) {
      joint.m_jointDamping = std::stod(damping_str);
    }

    // Get joint friction
    const char* friction_str = prop_xml->Attribute("friction");
    if (friction_str) {
      joint.m_jointFriction = std::stod(friction_str);
    }

    if (damping_str == NULL && friction_str == NULL) {
      Corrade::Utility::Debug() << "E - joint dynamics element specified with "
                                   "no damping and no friction";
      return false;
    }
  }

  return true;
}

bool URDFParser::parseSensor(UrdfModel& model,
                             UrdfLink& link,
                             UrdfJoint& joint,
                             tinyxml2::XMLElement* config) {
  // TODO: this
  return false;
}

void printLinkChildrenHelper(UrdfLink& link, std::string printPrefix = "") {
  // Corrade::Utility::Debug() << printPrefix<<"link "<< link.m_name;
  int childIndex = 0;
  for (auto& child : link.m_childJoints) {
    Corrade::Utility::Debug()
        << printPrefix << " child J(" << childIndex << "): " << child->m_name
        << " ->(" << child->m_childLinkName << ")";
    childIndex++;
  }
  childIndex = 0;
  for (auto& child : link.m_childLinks) {
    Corrade::Utility::Debug()
        << printPrefix << " child L(" << childIndex << "): " << child->m_name;
    printLinkChildrenHelper(*child, printPrefix + "  ");
    childIndex++;
  }
}

void UrdfModel::printKinematicChain() const {
  Corrade::Utility::Debug()
      << "------------------------------------------------------";
  Corrade::Utility::Debug()
      << "UrdfModel::printKinematicChain: model = " << m_name;
  int rootIndex = 0;
  for (auto& root : m_rootLinks) {
    Corrade::Utility::Debug()
        << "root L(" << rootIndex << "): " << root->m_name;
    printLinkChildrenHelper(*root);
    rootIndex++;
  }
  Corrade::Utility::Debug()
      << "------------------------------------------------------";
}

}  // namespace io
}  // namespace esp
