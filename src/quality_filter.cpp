#include <quality_filter.h>

#define HL_QUALITY_FILTER_DEFAULT_ENABLED   false
#define HL_QUALITY_FILTER_DEFAULT_MIN       0.f
#define HL_QUALITY_FILTER_DEFAULT_MAX       0.8f
#define HL_QUALITY_FILTER_DEFAULT_OP        Operator::Inside

namespace HexaLab {
    void QualityFilter::on_mesh_set(Mesh& mesh) {
        this->enabled               = HL_QUALITY_FILTER_DEFAULT_ENABLED;
        this->quality_threshold_min = HL_QUALITY_FILTER_DEFAULT_MIN;
        this->quality_threshold_max = HL_QUALITY_FILTER_DEFAULT_MAX;
        this->op                    = HL_QUALITY_FILTER_DEFAULT_OP;
    }

    void QualityFilter::filter(Mesh& mesh) {
        if (!this->enabled)
        	return;
        for (size_t i = 0; i < mesh.hexas.size(); ++i) {
            bool is_filtered;
        	switch(this->op) {
        	case Operator::Inside:
        		is_filtered = mesh.hexa_quality[i] < quality_threshold_min || mesh.hexa_quality[i] > quality_threshold_max;
        		break;
        	case Operator::Outside:
        		is_filtered = mesh.hexa_quality[i] > quality_threshold_min && mesh.hexa_quality[i] < quality_threshold_max;
        		break;
        	}
            if (is_filtered) {
                mesh.mark(mesh.hexas[i]);
            }
        }
    }
}
