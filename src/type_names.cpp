// BroWatch — display names for detection types. See include/type_names.h.
#include "type_names.h"
#include "settings.h"

namespace TypeNames {

const char* ru(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "ФЛОК";
        case DetectionType::AXON:        return "АКСОН";
        case DetectionType::META:        return "МЕТА";
        case DetectionType::SKIMMER:     return "СКИММЕР";
        case DetectionType::RAVEN:       return "РЕЙВЕН";
        case DetectionType::AIRTAG:      return "ЭЙРТАГ";
        case DetectionType::DRONE:       return "ДРОН";
        case DetectionType::ALPR:        return "АЛПР";
        case DetectionType::CAMERA:      return "КАМЕРА";
        case DetectionType::SAMSUNG_TAG: return "СМАРТТАГ";
        case DetectionType::GOOGLE_TAG:  return "ГУГЛ-ТАГ";
        case DetectionType::TILE:        return "ТАЙЛ";
        case DetectionType::RING:        return "РИНГ";
        case DetectionType::DEAUTH:      return "ДЕАУТ";
        case DetectionType::EVILTWIN:    return "ДВОЙНИК";
        case DetectionType::IBEACON:     return "МАЯК";
        case DetectionType::HACKER:      return "ХАКЕР";
        default:                         return "НЕИЗВЕСТНО";
    }
}

const char* cellRu(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "ФЛОК";
        case DetectionType::AXON:        return "АКСОН";
        case DetectionType::META:        return "ОЧКИ";
        case DetectionType::SKIMMER:     return "СКИМ";
        case DetectionType::RAVEN:       return "РЕЙВЕН";
        case DetectionType::AIRTAG:      return "ТРЕКЕР";
        case DetectionType::DRONE:       return "ДРОН";
        case DetectionType::ALPR:        return "АЛПР";
        case DetectionType::CAMERA:      return "КАМ";
        case DetectionType::SAMSUNG_TAG: return "СТАГ";
        case DetectionType::GOOGLE_TAG:  return "ГТАГ";
        case DetectionType::TILE:        return "ТАЙЛ";
        case DetectionType::RING:        return "РИНГ";
        case DetectionType::DEAUTH:      return "ДЕАУТ";
        case DetectionType::EVILTWIN:    return "ТВИН";
        case DetectionType::IBEACON:     return "МАЯК";
        case DetectionType::HACKER:      return "ХАК";
        default:                         return "?";
    }
}

const char* headlineRu(DetectionType t) {
    switch (t) {
        case DetectionType::FLOCK:       return "ФЛОК-КАМЕРА";
        case DetectionType::AXON:        return "АКСОН";
        case DetectionType::META:        return "МЕТА-ОЧКИ";
        case DetectionType::SKIMMER:     return "СКИММЕР";
        case DetectionType::RAVEN:       return "РЕЙВЕН";
        case DetectionType::AIRTAG:      return "ЭЙРТАГ";
        case DetectionType::DRONE:       return "ДРОН";
        case DetectionType::ALPR:        return "АЛПР";
        case DetectionType::CAMERA:      return "КАМЕРА";
        case DetectionType::SAMSUNG_TAG: return "СМАРТТАГ";
        case DetectionType::GOOGLE_TAG:  return "ГУГЛ-ТАГ";
        case DetectionType::TILE:        return "ТАЙЛ";
        case DetectionType::RING:        return "РИНГ";
        case DetectionType::DEAUTH:      return "ДЕАУТ";
        case DetectionType::EVILTWIN:    return "ДВОЙНИК";
        case DetectionType::IBEACON:     return "МАЯК";
        case DetectionType::HACKER:      return "ХАКЕР";
        default:                         return "НЕИЗВЕСТНО";
    }
}

const char* display(DetectionType t) {
    return Settings::lang() == 1 ? ru(t) : detectionTypeName(t);
}

}  // namespace TypeNames
